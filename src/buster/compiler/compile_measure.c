// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// measure-compile (#518, #146): whole compiles, each a process of its own, and where their time goes.
//
//   build/compiler measure-compile [--runs N] [--warmups N] [--reference PATH [--reference-option OPTION]... |
//                                  --variant OPTION]... [--directory DIR] [--keep] -- ARGUMENT...
//
// Runs this executable on ARGUMENT... -o DIR/default.o, the default compile, and on --time-stages ARGUMENT...
// -o DIR/stages.o, whose report (c_check_clock.c) says where its time went; and each reference, four at most: a
// --reference, PATH on its --reference-options and ARGUMENT... -o DIR/reference-K.o (`tcc` with `-c ...`, or `clang`
// with `--reference-option -O0`, #146), a name without a slash looked up in PATH as a shell does; or a --variant, this
// executable on OPTION ARGUMENT... -o DIR/reference-K.o (#172's `--variant --tier1-schedule=post`). The warm-ups run in
// that order, then `--runs` rounds (11) in an order of a Williams design by round (profile_ab_tree's), so that each
// compile follows each other equally often. Each process's wall time is from its release (it waits on a pipe until its
// counters are open, as time-runs' do) to wait4's return; its user cycles and instructions come from perf_event_open,
// from its exec to its exit, where the kernel allows them (otherwise "uncounted"). Its rusage peak includes pre-exec
// memory and can include waited descendants; it is not an executable-only peak. Only --time-stages supplies a separate
// approximate post-exec RSS report.
// It prints every run, each compile's medians with their ranges, what --time-stages costs (its median against the
// default's), the default against each reference, each compile's time and user cycles per preprocessed token (the
// report's `tokens`, #146), and each stage's median, its range, its share of the median span and its cycles per token
// (the preprocessor's phases, the reader's expressions' walk, `check-input.expressions`, and the lowering's phases,
// under their stages): TSC ticks at the report's `tsc-mhz`, and core cycles at the --time-stages processes' median
// cycles (user and kernel) per wall microsecond, which takes the stage as running at the process's average clock (one
// pinned thread).
// Then the ranking, with the time each process spent outside the clock's span (exec, the loader, main's dispatch and
// exit), and the lowering's coverage, the first report's `lowering-nodes` line. Every process must exit with status 0,
// every run's default and --time-stages objects must be the same bytes, and every report must be whole, or the command
// fails. The caller pins it (taskset): the processes inherit its CPUs. DIR is a new directory under --directory
// ($TMPDIR, or /tmp), removed at the end unless --keep.
// docs/compile-time.md has the 9700X's numbers for sqlite3.c, against tcc, clang -O0 and gcc -O0.

#include "buster/compiler/test_paths.h"
#define BUSTER_MEASURE_REFERENCES 4
#define BUSTER_MEASURE_REFERENCE_OPTIONS 8
#define BUSTER_MEASURE_COMPILES (2 + BUSTER_MEASURE_REFERENCES)
#define BUSTER_MEASURE_MAX_RUNS 1000

// A compiler the default is compared with: its executable, the options before the arguments, and its name in the report.
typedef struct MeasureReference MeasureReference;
struct MeasureReference
{
    const char* executable;
    char* options[BUSTER_MEASURE_REFERENCE_OPTIONS];
    uint32_t option_count;
    // A --variant: this executable, named by its option.
    bool variant;
};

typedef struct MeasureCompile MeasureCompile;
struct MeasureCompile
{
    char* const* arguments;
    uint32_t argument_count;
    MeasureReference references[BUSTER_MEASURE_REFERENCES];
    uint32_t reference_count;
    uint32_t runs;
    uint32_t warmups;
    char directory[PATH_MAX];
    char outputs[BUSTER_MEASURE_COMPILES][PATH_MAX];
    // Each compile's name: `default`, `time-stages`, and each reference's executable's base name and options (`clang
    // -O0`), a variant's `variant OPTION`.
    char names[BUSTER_MEASURE_COMPILES][160];
    char errors[PATH_MAX];
};

// A counter of the child's cycles in user and kernel mode alike, from its exec to its exit, with every thread it starts,
// or -1 where perf_event_open refuses it: what measure-compile takes the core's clock from.
static int measure_compile_open_cycles(pid_t child)
{
    struct perf_event_attr attribute = { 0 };
    attribute.size = sizeof(attribute);
    attribute.type = PERF_TYPE_HARDWARE;
    attribute.config = PERF_COUNT_HW_CPU_CYCLES;
    attribute.disabled = 1;
    attribute.enable_on_exec = 1;
    attribute.inherit = 1;
    attribute.exclude_hv = 1;
    attribute.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(SYS_perf_event_open, &attribute, child, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

// Runs compile `c` once, its standard output on /dev/null and its errors into `m->errors`, with its user cycles and
// instructions counted (profile_open_counter) where the kernel allows, and into `*all_cycles` its cycles in both modes
// (0 where uncounted). The child waits on a pipe until its counters are open, then execs.
static ProfileTimed measure_compile_one(const MeasureCompile* m, uint32_t c, char** argv, uint64_t* all_cycles)
{
    const MeasureReference* reference = c >= 2 ? &m->references[c - 2] : NULL;
    const char* executable = reference != NULL ? reference->executable : "/proc/self/exe";
    uint32_t count = 0;
    argv[count++] = (char*)executable;
    if (c == 1)
    {
        argv[count++] = "--time-stages";
    }
    for (uint32_t o = 0; reference != NULL && o < reference->option_count; o += 1)
    {
        argv[count++] = reference->options[o];
    }
    for (uint32_t a = 0; a < m->argument_count; a += 1)
    {
        argv[count++] = m->arguments[a];
    }
    argv[count++] = "-o";
    argv[count++] = (char*)m->outputs[c];
    argv[count] = NULL;
    ProfileTimed result = { .status = -1 };
    int release[2] = { -1, -1 };
    const bool piped = pipe(release) == 0 && fcntl(release[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(release[1], F_SETFD, FD_CLOEXEC) == 0;
    const pid_t child = fork();
    if (child == 0)
    {
        char byte = 0;
        if (piped)
        {
            close(release[1]);
        }
        // An end of file, the parent gone before it released the child, runs nothing.
        const bool released = !piped || read(release[0], &byte, 1) == 1;
        const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        const int errors = open(m->errors, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (released && null != -1 && errors != -1 && dup2(null, STDIN_FILENO) != -1 && dup2(null, STDOUT_FILENO) != -1 &&
            dup2(errors, STDERR_FILENO) != -1)
        {
            execvp(executable, argv);
        }
        _exit(127);
    }
    const int cycles = child != -1 ? profile_open_counter(child, PERF_COUNT_HW_CPU_CYCLES) : -1;
    const int instructions = child != -1 ? profile_open_counter(child, PERF_COUNT_HW_INSTRUCTIONS) : -1;
    const int both = child != -1 ? measure_compile_open_cycles(child) : -1;
    const uint64_t start = profile_now_ns();
    if (piped)
    {
        close(release[0]);
        if (child != -1 && write(release[1], "x", 1) != 1)
        {
            kill(child, SIGKILL);
        }
        close(release[1]);
    }
    struct rusage usage;
    int status = 0;
    const bool waited = child != -1 && wait4(child, &status, 0, &usage) == child;
    const uint64_t wall_ns = profile_now_ns() - start;
    uint64_t cycle_count = 0;
    uint64_t instruction_count = 0;
    const bool counted_cycles = profile_read_counter(cycles, &cycle_count);
    const bool counted_instructions = profile_read_counter(instructions, &instruction_count);
    const bool counted_both = profile_read_counter(both, all_cycles);
    *all_cycles = counted_both ? *all_cycles : 0;
    if (waited)
    {
        result = (ProfileTimed){
            .wall_ns = wall_ns,
            .user_us = (uint64_t)usage.ru_utime.tv_sec * 1000000ull + (uint64_t)usage.ru_utime.tv_usec,
            .system_us = (uint64_t)usage.ru_stime.tv_sec * 1000000ull + (uint64_t)usage.ru_stime.tv_usec,
            .peak_kb = (uint64_t)usage.ru_maxrss,
            .minor_faults = (uint64_t)usage.ru_minflt,
            .major_faults = (uint64_t)usage.ru_majflt,
            .cycles = cycle_count,
            .instructions = instruction_count,
            .counted = counted_cycles && counted_instructions,
            .status = status,
        };
    }
    return result;
}

// The whole file at `path`, NUL-terminated, from `arena`, or null.
static char* measure_compile_read(Arena* arena, const char* path)
{
    char* text = NULL;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (file != -1 && fstat(file, &status) == 0 && status.st_size >= 0)
    {
        const uint64_t size = (uint64_t)status.st_size;
        char* bytes = BUSTER_ARENA_PUSH_ARRAY(arena, char, size + 1);
        uint64_t done = 0;
        ssize_t got = 1;
        while (bytes != NULL && done < size && got > 0)
        {
            got = read(file, bytes + done, size - done);
            done += got > 0 ? (uint64_t)got : 0;
        }
        if (bytes != NULL && done == size)
        {
            bytes[size] = 0;
            text = bytes;
        }
    }
    if (file != -1)
    {
        close(file);
    }
    return text;
}

// Whether the files at `a` and `b` have the same bytes.
static bool measure_compile_same(Arena* arena, const char* a, const char* b)
{
    const uint64_t mark = arena->length;
    struct stat first;
    struct stat second;
    bool same = stat(a, &first) == 0 && stat(b, &second) == 0 && first.st_size == second.st_size;
    const char* left = same ? measure_compile_read(arena, a) : NULL;
    const char* right = same ? measure_compile_read(arena, b) : NULL;
    same = left != NULL && right != NULL && memcmp(left, right, (size_t)first.st_size) == 0;
    arena_pop_to(arena, mark);
    return same;
}

// The median of `count` doubles, sorted in place (insertion sort: a few dozen runs).
static double measure_compile_median(double* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const double value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
    return count != 0 ? values[count / 2] : 0;
}

// A run's row of --time-stages' report: each stage's milliseconds, the preprocessor's and the lowering's rows, the span, the process's
// wall time outside it, each stage's system milliseconds, minor faults and peak KB added, the reader's expressions' walk
// inside its stage (#1038), and the report's TSC rate and preprocessed tokens (#146).
#define BUSTER_MEASURE_SPAN (CHECK_STAGE_COUNT + BUSTER_CHECK_CLOCK_PHASES)
#define BUSTER_MEASURE_OUTSIDE (BUSTER_MEASURE_SPAN + 1)
#define BUSTER_MEASURE_USAGE (BUSTER_MEASURE_SPAN + 2)
#define BUSTER_MEASURE_WALK (BUSTER_MEASURE_USAGE + 3 * CHECK_STAGE_COUNT)
// The walk's parts, then its system milliseconds and minor faults (check_clock_read_walk).
#define BUSTER_MEASURE_PARTS (BUSTER_MEASURE_WALK + 1)
#define BUSTER_MEASURE_TSC (BUSTER_MEASURE_PARTS + BUSTER_CHECK_CLOCK_WALK_PARTS + 2)
#define BUSTER_MEASURE_TOKENS (BUSTER_MEASURE_TSC + 1)
// The --time-stages process's core clock in MHz, -1 uncounted.
#define BUSTER_MEASURE_CLOCK (BUSTER_MEASURE_TOKENS + 1)
#define BUSTER_MEASURE_ROW (BUSTER_MEASURE_CLOCK + 1)

// The cycles per token of `milliseconds`: TSC ticks at `tsc_mhz` and core cycles at `core_mhz` (-1 where uncounted).
static void measure_compile_per_token(double milliseconds, double tsc_mhz, double core_mhz, double tokens, double* tsc, double* core)
{
    *tsc = tokens > 0 ? milliseconds * tsc_mhz * 1e3 / tokens : -1;
    *core = tokens > 0 && core_mhz > 0 ? milliseconds * core_mhz * 1e3 / tokens : -1;
}

// The warm-ups and the rounds, then the report. Returns how many failures it found.
static uint64_t measure_compile_run(Arena* arena, MeasureCompile* m)
{
    printf("memory: wait4 peaks include pre-exec memory and waited descendants; executable-only peaks are not measured by wait4\n");
    printf("memory: --time-stages peak growth is approximate post-exec RSS; -1 means unavailable, not zero\n");
    printf("stage system time: low-confidence getrusage(RUSAGE_SELF) deltas between resource marks, -1 when unavailable; do not subtract them from stage elapsed time or rank stages by them\n");
    const uint32_t compiles = 2 + m->reference_count;
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, (uint64_t)m->runs * BUSTER_MEASURE_COMPILES);
    double* rows = BUSTER_ARENA_PUSH_ARRAY(arena, double, (uint64_t)m->runs * BUSTER_MEASURE_ROW);
    double* values = BUSTER_ARENA_PUSH_ARRAY(arena, double, m->runs + 1);
    char** argv = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)m->argument_count + BUSTER_MEASURE_REFERENCE_OPTIONS + 6);
    uint64_t failures = timed == NULL || rows == NULL || values == NULL || argv == NULL;
    // The lowering's coverage, from the first --time-stages report: its `lowering-nodes` line (#624's fragments' and #1048's
    // templates' shares of the evaluated nodes), which every run repeats, kept to print with the medians.
    char coverage_line[512] = { 0 };
    bool covered = false;
    bool expression_work = false;
    for (uint32_t run = 0; failures == 0 && run < m->warmups + m->runs; run += 1)
    {
        const bool warmup = run < m->warmups;
        for (uint32_t position = 0; failures == 0 && position < compiles; position += 1)
        {
            const uint32_t c = warmup ? position : (uint32_t)profile_ab_tree(run - m->warmups, position, compiles);
            uint64_t all_cycles = 0;
            const ProfileTimed one = measure_compile_one(m, c, argv, &all_cycles);
            const bool exited = one.status != -1 && WIFEXITED(one.status) && WEXITSTATUS(one.status) == 0;
            const uint64_t mark = arena->length;
            const char* errors = exited && c == 1 ? measure_compile_read(arena, m->errors) : NULL;
            double scratch[BUSTER_MEASURE_ROW] = { 0 };
            double* row = warmup ? scratch : rows + (uint64_t)(run - m->warmups) * BUSTER_MEASURE_ROW;
            if (c == 1)
            {
                row[BUSTER_MEASURE_TSC] = check_clock_read_process(errors, "tsc-mhz");
                row[BUSTER_MEASURE_TOKENS] = check_clock_read_process(errors, "tokens");
            }
            const bool reported = c != 1 || (errors != NULL && check_clock_read(errors, row, row + CHECK_STAGE_COUNT, row + BUSTER_MEASURE_SPAN,
                                                                                 row + BUSTER_MEASURE_WALK, row + BUSTER_MEASURE_USAGE) &&
                                             check_clock_read_walk(errors, row + BUSTER_MEASURE_PARTS) && row[BUSTER_MEASURE_TSC] > 0 &&
                                             row[BUSTER_MEASURE_TOKENS] >= 0);
            if (c == 1)
            {
                // The core's clock across the process: its cycles in both modes per microsecond of its wall time, which a
                // pinned process spends running, but for its exec's and exit's waits (-1 uncounted).
                row[BUSTER_MEASURE_CLOCK] = all_cycles != 0 && one.wall_ns != 0 ? (double)all_cycles * 1e3 / (double)one.wall_ns : -1;
                row[BUSTER_MEASURE_OUTSIDE] = (double)one.wall_ns / 1e6 - row[BUSTER_MEASURE_SPAN];
                expression_work = check_clock_walk_work(errors);
            }
            const char* coverage = errors != NULL && !covered ? strstr(errors, "lowering-nodes\t") : NULL;
            if (coverage != NULL)
            {
                const char* end = strchr(coverage, '\n');
                const ptrdiff_t length = end != NULL ? end - coverage : (ptrdiff_t)strlen(coverage);
                snprintf(coverage_line, sizeof(coverage_line), "%.*s", (int)length, coverage);
                covered = true;
            }
            arena_pop_to(arena, mark);
            if (!exited || !reported)
            {
                failures += 1;
                printf("%s %u %s: FAILED (%s)\n", warmup ? "warm-up" : "run", (warmup ? run : run - m->warmups) + 1, m->names[c],
                       !exited ? "the process failed" : "no whole --time-stages report");
            }
            if (!warmup)
            {
                timed[(uint64_t)(run - m->warmups) * BUSTER_MEASURE_COMPILES + c] = one;
                printf("run %u %s: %.3f ms wall, %.3f ms user, %.3f ms system, %llu KiB wait4 peak, %llu minor faults, %llu major", run - m->warmups + 1,
                       m->names[c], (double)one.wall_ns / 1e6, (double)one.user_us / 1e3, (double)one.system_us / 1e3,
                       (unsigned long long)one.peak_kb, (unsigned long long)one.minor_faults, (unsigned long long)one.major_faults);
                if (one.counted)
                {
                    printf(", %llu user cycles, %llu user instructions\n", (unsigned long long)one.cycles, (unsigned long long)one.instructions);
                }
                else
                {
                    printf(", uncounted\n");
                }
            }
        }
        // The default's object must be the --time-stages one's, byte for byte.
        if (failures == 0 && !measure_compile_same(arena, m->outputs[0], m->outputs[1]))
        {
            failures += 1;
            printf("%s %u: FAILED (the default's object isn't --time-stages')\n", warmup ? "warm-up" : "run", (warmup ? run : run - m->warmups) + 1);
        }
    }
    // The preprocessed tokens, which every run's report must give alike.
    const double tokens = failures == 0 && m->runs != 0 ? rows[BUSTER_MEASURE_TOKENS] : 0;
    for (uint32_t r = 0; failures == 0 && r < m->runs; r += 1)
    {
        if (rows[(uint64_t)r * BUSTER_MEASURE_ROW + BUSTER_MEASURE_TOKENS] != tokens)
        {
            failures += 1;
            printf("run %u: FAILED (its report's tokens aren't the first run's)\n", r + 1);
        }
    }
    // Each compile's medians: wall, user and system time, peak, faults, and where every run was counted, user cycles and
    // instructions (-1 otherwise).
    double medians[BUSTER_MEASURE_COMPILES][8] = { 0 };
    for (uint32_t c = 0; failures == 0 && c < compiles; c += 1)
    {
        bool counted = true;
        for (uint32_t r = 0; r < m->runs; r += 1)
        {
            counted = counted && timed[(uint64_t)r * BUSTER_MEASURE_COMPILES + c].counted;
        }
        double low = 0;
        double high = 0;
        for (uint32_t field = 0; field < 8; field += 1)
        {
            for (uint32_t r = 0; r < m->runs; r += 1)
            {
                const ProfileTimed* one = &timed[(uint64_t)r * BUSTER_MEASURE_COMPILES + c];
                const double all[8] = { (double)one->wall_ns / 1e6, (double)one->user_us / 1e3, (double)one->system_us / 1e3, (double)one->peak_kb,
                                        (double)one->minor_faults, (double)one->major_faults, (double)one->cycles, (double)one->instructions };
                values[r] = all[field];
            }
            medians[c][field] = field >= 6 && !counted ? -1 : measure_compile_median(values, m->runs);
            low = field == 0 ? values[0] : low;
            high = field == 0 ? values[m->runs - 1] : high;
        }
        printf("%s: median of %u: %.3f ms wall (%.3f to %.3f), %.3f ms user, %.3f ms system, %.0f KiB wait4 peak, %.0f minor faults, %.0f major", m->names[c],
               m->runs, medians[c][0], low, high, medians[c][1], medians[c][2], medians[c][3], medians[c][4], medians[c][5]);
        if (counted)
        {
            printf(", %.1f M user cycles, %.1f M user instructions\n", medians[c][6] / 1e6, medians[c][7] / 1e6);
        }
        else
        {
            printf(", uncounted\n");
        }
    }
    if (failures == 0)
    {
        printf("--time-stages costs %.3f ms (%.2f%%) of the default's median wall time\n", medians[1][0] - medians[0][0],
               medians[0][0] > 0 ? (medians[1][0] - medians[0][0]) * 100.0 / medians[0][0] : 0.0);
    }
    if (failures == 0 && covered)
    {
        printf("lowering coverage: %s\n", coverage_line);
    }
    for (uint32_t c = 2; failures == 0 && c < compiles; c += 1)
    {
        printf("default against %s: %.3f ms against %.3f ms, %.2f times, %.3f ms apart\n", m->names[c], medians[0][0], medians[c][0],
               medians[c][0] > 0 ? medians[0][0] / medians[c][0] : 0.0, medians[0][0] - medians[c][0]);
    }
    // Each compile's work per preprocessed token: its median wall time and its median user cycles (-1 uncounted).
    if (failures == 0)
    {
        printf("per token: %.0f preprocessed tokens (--time-stages' `tokens`)\n", tokens);
    }
    for (uint32_t c = 0; failures == 0 && c < compiles; c += 1)
    {
        printf("  %-30s %9.1f ns wall  %9.1f user cycles  %9.1f user instructions\n", m->names[c], tokens > 0 ? medians[c][0] * 1e6 / tokens : -1.0,
               tokens > 0 && medians[c][6] >= 0 ? medians[c][6] / tokens : -1.0, tokens > 0 && medians[c][7] >= 0 ? medians[c][7] / tokens : -1.0);
    }
    // Each column's median and range over the runs.
    double median[BUSTER_MEASURE_ROW] = { 0 };
    double low[BUSTER_MEASURE_ROW] = { 0 };
    double high[BUSTER_MEASURE_ROW] = { 0 };
    for (uint32_t i = 0; failures == 0 && i < BUSTER_MEASURE_ROW; i += 1)
    {
        const bool usage_column = i >= BUSTER_MEASURE_USAGE;
        bool unavailable = false;
        for (uint32_t r = 0; r < m->runs; r += 1)
        {
            values[r] = rows[(uint64_t)r * BUSTER_MEASURE_ROW + i];
            unavailable = unavailable || (usage_column && values[r] < 0);
        }
        // Do not sort unavailable sentinels into a plausible median of the remaining system-time, fault or RSS readings.
        median[i] = unavailable ? -1 : measure_compile_median(values, m->runs);
        low[i] = unavailable ? -1 : values[0];
        high[i] = unavailable ? -1 : values[m->runs - 1];
    }
    // The core's clock in the --time-stages processes: the median of their cycles per wall microsecond (-1 uncounted).
    const double core_mhz = median[BUSTER_MEASURE_CLOCK];
    if (failures == 0)
    {
        const double span = median[BUSTER_MEASURE_SPAN];
        const double tsc_mhz = median[BUSTER_MEASURE_TSC];
        double sum = 0;
        printf("cycles per token: TSC ticks at %.1f MHz; core cycles at %.1f MHz, the --time-stages processes' median cycles (user and kernel) per wall ms (-1 uncounted)\n",
               tsc_mhz, core_mhz);
        printf("--time-stages' stages, medians of %u runs: ms (range), share of the median span; low-confidence system ms, minor faults, approximate post-exec peak KiB added (-1 unavailable); TSC ticks and core cycles per token\n", m->runs);
        for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
        {
            const double* used = median + BUSTER_MEASURE_USAGE + 3 * s;
            double tsc = 0;
            double core = 0;
            measure_compile_per_token(median[s], tsc_mhz, core_mhz, tokens, &tsc, &core);
            sum += median[s];
            printf("  %-22s %9.3f  (%.3f to %.3f)  %5.1f%%  %8.3f  %7.0f  %8.0f  %8.1f  %8.1f\n", check_stage_names[s], median[s], low[s], high[s],
                   span > 0 ? median[s] * 100.0 / span : 0.0, used[0], used[1], used[2], tsc, core);
            for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
            {
                const uint32_t index = CHECK_STAGE_COUNT + p;
                if (check_clock_phase_stages[p] == s)
                {
                    measure_compile_per_token(median[index], tsc_mhz, core_mhz, tokens, &tsc, &core);
                    printf("    %-20s %9.3f  (%.3f to %.3f)  %5.1f%%  %8.1f  %8.1f\n", check_clock_phase_names[p], median[index], low[index], high[index],
                           span > 0 ? median[index] * 100.0 / span : 0.0, tsc, core);
                }
            }
            if (s == CHECK_STAGE_CHECK_INPUT)
            {
                measure_compile_per_token(median[BUSTER_MEASURE_WALK], tsc_mhz, core_mhz, tokens, &tsc, &core);
                printf("    %-20s %9.3f  (%.3f to %.3f)  %5.1f%%  %8.1f  %8.1f\n", "check-input.expressions", median[BUSTER_MEASURE_WALK], low[BUSTER_MEASURE_WALK],
                       high[BUSTER_MEASURE_WALK], span > 0 ? median[BUSTER_MEASURE_WALK] * 100.0 / span : 0.0, tsc, core);
                check_clock_print_walk(stdout, median + BUSTER_MEASURE_PARTS, low + BUSTER_MEASURE_PARTS, high + BUSTER_MEASURE_PARTS, span, expression_work);
            }
        }
        double tsc = 0;
        double core = 0;
        measure_compile_per_token(span, tsc_mhz, core_mhz, tokens, &tsc, &core);
        printf("  %-22s %9.3f  (%.3f to %.3f)  the stages' medians add up to %.3f; %.1f TSC ticks and %.1f core cycles per token\n", "span", span,
               low[BUSTER_MEASURE_SPAN], high[BUSTER_MEASURE_SPAN], sum, tsc, core);
        printf("  %-22s %9.3f  (%.3f to %.3f)  the process's wall time outside the span\n", "outside", median[BUSTER_MEASURE_OUTSIDE],
               low[BUSTER_MEASURE_OUTSIDE], high[BUSTER_MEASURE_OUTSIDE]);
        // The ranking: the largest remaining stage each time.
        bool taken[CHECK_STAGE_COUNT] = { 0 };
        printf("ranked:");
        for (uint32_t k = 0; k < CHECK_STAGE_COUNT; k += 1)
        {
            uint32_t best = CHECK_STAGE_COUNT;
            for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
            {
                best = !taken[s] && (best == CHECK_STAGE_COUNT || median[s] > median[best]) ? s : best;
            }
            taken[best] = true;
            if (median[best] > 0)
            {
                printf(" %s %.1f%%", check_stage_names[best], span > 0 ? median[best] * 100.0 / span : 0.0);
            }
        }
        printf("\n");
    }
    printf("measure-compile: %llu failures\n", (unsigned long long)failures);
    return failures;
}

// Names compile `c` (MeasureCompile.names): a reference by its executable's base name and options, a variant by its option.
static void measure_compile_name(MeasureCompile* m, uint32_t c)
{
    const MeasureReference* reference = c >= 2 ? &m->references[c - 2] : NULL;
    const bool variant = reference != NULL && reference->variant;
    const char* slash = reference != NULL ? strrchr(reference->executable, '/') : NULL;
    const char* base = reference == NULL ? (c == 0 ? "default" : "time-stages") : variant ? "variant" : slash != NULL ? slash + 1 : reference->executable;
    int length = snprintf(m->names[c], sizeof(m->names[c]), "%s", base);
    for (uint32_t o = 0; reference != NULL && o < reference->option_count && length >= 0 && (size_t)length < sizeof(m->names[c]); o += 1)
    {
        const int added = snprintf(m->names[c] + length, sizeof(m->names[c]) - (size_t)length, " %s", reference->options[o]);
        length = added >= 0 ? length + added : added;
    }
}

static int run_measure_compile_command(int argc, char** argv)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    MeasureCompile m = { .runs = 11, .warmups = 1 };
    const char* parent = oracle_temporary_directory();
    bool keep = false;
    bool valid = arena.base != 0;
    int i = 1;
    while (valid && i < argc && strcmp(argv[i], "--") != 0)
    {
        uint64_t value = 0;
        const bool more = i + 1 < argc;
        MeasureReference* last = m.reference_count != 0 ? &m.references[m.reference_count - 1] : NULL;
        if (strcmp(argv[i], "--runs") == 0 && more && profile_count_option(argv[i + 1], &value) && value <= BUSTER_MEASURE_MAX_RUNS)
        {
            m.runs = (uint32_t)value;
            i += 2;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && more && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 1000)
        {
            m.warmups = (uint32_t)value;
            i += 2;
        }
        else if ((strcmp(argv[i], "--reference") == 0 || strcmp(argv[i], "--variant") == 0) && more && m.reference_count < BUSTER_MEASURE_REFERENCES)
        {
            // A variant is this executable with its option before the arguments.
            const bool variant = strcmp(argv[i], "--variant") == 0;
            MeasureReference* reference = &m.references[m.reference_count];
            reference->executable = variant ? "/proc/self/exe" : argv[i + 1];
            reference->options[0] = argv[i + 1];
            reference->option_count = variant;
            reference->variant = variant;
            m.reference_count += 1;
            i += 2;
        }
        else if (strcmp(argv[i], "--reference-option") == 0 && more && last != NULL && last->option_count < BUSTER_MEASURE_REFERENCE_OPTIONS)
        {
            last->options[last->option_count] = argv[i + 1];
            last->option_count += 1;
            i += 2;
        }
        else if (strcmp(argv[i], "--directory") == 0 && more)
        {
            parent = argv[i + 1];
            i += 2;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && i + 1 < argc;
    m.arguments = valid ? argv + i + 1 : NULL;
    m.argument_count = valid ? (uint32_t)(argc - i - 1) : 0;
    const int length = valid ? snprintf(m.directory, sizeof(m.directory), "%s/measure-compile-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 32 < sizeof(m.directory);
    const bool made = valid && mkdtemp(m.directory) != NULL;
    const uint32_t compiles = 2 + m.reference_count;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: measure-compile [--runs N] [--warmups N] [--reference PATH [--reference-option OPTION]... | --variant OPTION]... "
                        "[--directory DIR] [--keep] -- ARGUMENT...\n"
                        "  at most %u references, each with at most %u options\n",
                BUSTER_MEASURE_REFERENCES, BUSTER_MEASURE_REFERENCE_OPTIONS);
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        for (uint32_t c = 0; c < compiles; c += 1)
        {
            const char* const fixed[2] = { "default", "stages" };
            if (c < 2)
            {
                snprintf(m.outputs[c], sizeof(m.outputs[c]), "%s/%s.o", m.directory, fixed[c]);
            }
            else
            {
                snprintf(m.outputs[c], sizeof(m.outputs[c]), "%s/reference-%u.o", m.directory, c - 1);
            }
            measure_compile_name(&m, c);
        }
        snprintf(m.errors, sizeof(m.errors), "%s/errors.txt", m.directory);
        printf("measure-compile: %u warm-ups, %u runs, %u references", m.warmups, m.runs, m.reference_count);
        for (uint32_t c = 2; c < compiles; c += 1)
        {
            printf("%s %s (%s)", c == 2 ? ":" : ",", m.names[c], m.references[c - 2].executable);
        }
        printf(", in %s\n", m.directory);
        result = measure_compile_run(&arena, &m) == 0 ? 0 : 1;
        if (!keep)
        {
            for (uint32_t c = 0; c < compiles; c += 1)
            {
                unlink(m.outputs[c]);
            }
            unlink(m.errors);
            rmdir(m.directory);
        }
    }
    arena_release(&arena);
    return result;
}
