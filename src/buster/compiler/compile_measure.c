// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-compile (#518): whole compiles, each a process of its own, and where their time goes.
//
//   build/compiler measure-compile [--runs N] [--warmups N] [--reference PATH] [--directory DIR] [--keep] -- ARGUMENT...
//
// Runs this executable on ARGUMENT... -o DIR/default.o, the default compile, and on --time-stages ARGUMENT...
// -o DIR/stages.o, whose report (c_check_clock.c) says where its time went; and with --reference, PATH on ARGUMENT...
// -o DIR/reference.o (`tcc` with `-c ...`, say), a name without a slash looked up in PATH as a shell does. The warm-ups run in that order, then `--runs` rounds (11) in an order
// that rotates by round (profile_ab_tree's), so that no compile always follows the same one. Each process's wall time
// is from fork to wait4's return. Its rusage peak includes pre-exec memory and can include waited descendants;
// it is not an executable-only peak. Only --time-stages supplies a separate approximate post-exec RSS report.
// It prints every run, each compile's medians with their ranges, what --time-stages costs (its median against the
// default's), the default against the reference, and each stage's median, its range and its share of the median
// span, ranked, with the time each process spent outside the clock's span (exec, the loader, main's dispatch and
// exit). Every process must exit with status 0, every run's two objects must be the same bytes, and every report must
// be whole, or the command fails. The caller pins it (taskset): the processes inherit its CPUs. DIR is a new directory
// under --directory ($TMPDIR, or /tmp), removed at the end unless --keep.
// docs/compile-time.md has the 9700X's numbers for sqlite3.c, against tcc.

#define BUSTER_MEASURE_COMPILES 3
#define BUSTER_MEASURE_MAX_RUNS 1000

typedef struct MeasureCompile MeasureCompile;
struct MeasureCompile
{
    char* const* arguments;
    uint32_t argument_count;
    const char* reference;
    uint32_t runs;
    uint32_t warmups;
    char directory[PATH_MAX];
    char outputs[BUSTER_MEASURE_COMPILES][PATH_MAX];
    char errors[PATH_MAX];
};

static const char* const measure_compile_names[BUSTER_MEASURE_COMPILES] = { "default", "time-stages", "reference" };

// Runs compile `c` once, its standard output on /dev/null and its errors into `m->errors`.
static ProfileTimed measure_compile_one(const MeasureCompile* m, uint32_t c, char** argv)
{
    const char* executable = c == 2 ? m->reference : "/proc/self/exe";
    uint32_t count = 0;
    argv[count++] = (char*)executable;
    if (c == 1)
    {
        argv[count++] = "--time-stages";
    }
    for (uint32_t a = 0; a < m->argument_count; a += 1)
    {
        argv[count++] = m->arguments[a];
    }
    argv[count++] = "-o";
    argv[count++] = (char*)m->outputs[c];
    argv[count] = NULL;
    ProfileTimed result = { .status = -1 };
    const uint64_t start = profile_now_ns();
    const pid_t child = fork();
    if (child == 0)
    {
        const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        const int errors = open(m->errors, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (null != -1 && errors != -1 && dup2(null, STDIN_FILENO) != -1 && dup2(null, STDOUT_FILENO) != -1 && dup2(errors, STDERR_FILENO) != -1)
        {
            execvp(executable, argv);
        }
        _exit(127);
    }
    struct rusage usage;
    int status = 0;
    if (child != -1 && wait4(child, &status, 0, &usage) == child)
    {
        result = (ProfileTimed){
            .wall_ns = profile_now_ns() - start,
            .user_us = (uint64_t)usage.ru_utime.tv_sec * 1000000ull + (uint64_t)usage.ru_utime.tv_usec,
            .system_us = (uint64_t)usage.ru_stime.tv_sec * 1000000ull + (uint64_t)usage.ru_stime.tv_usec,
            .peak_kb = (uint64_t)usage.ru_maxrss,
            .minor_faults = (uint64_t)usage.ru_minflt,
            .major_faults = (uint64_t)usage.ru_majflt,
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

// A run's row of --time-stages' report: each stage's milliseconds, the preprocessor's rows, the span, the process's
// wall time outside it, and each stage's system milliseconds, minor faults and peak KB added.
#define BUSTER_MEASURE_SPAN (CHECK_STAGE_COUNT + BUSTER_CHECK_CLOCK_PHASES)
#define BUSTER_MEASURE_OUTSIDE (BUSTER_MEASURE_SPAN + 1)
#define BUSTER_MEASURE_USAGE (BUSTER_MEASURE_SPAN + 2)
#define BUSTER_MEASURE_ROW (BUSTER_MEASURE_USAGE + 3 * CHECK_STAGE_COUNT)

// The warm-ups and the rounds, then the report. Returns how many failures it found.
static uint64_t measure_compile_run(Arena* arena, MeasureCompile* m)
{
    printf("memory: wait4 peaks include pre-exec memory and waited descendants; executable-only peaks are not measured by wait4\n");
    printf("memory: --time-stages peak growth is approximate post-exec RSS; -1 means unavailable, not zero\n");
    const uint32_t compiles = m->reference != NULL ? 3 : 2;
    ProfileTimed* timed = BUSTER_ARENA_PUSH_ARRAY(arena, ProfileTimed, (uint64_t)m->runs * BUSTER_MEASURE_COMPILES);
    double* rows = BUSTER_ARENA_PUSH_ARRAY(arena, double, (uint64_t)m->runs * BUSTER_MEASURE_ROW);
    double* values = BUSTER_ARENA_PUSH_ARRAY(arena, double, m->runs + 1);
    char** argv = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)m->argument_count + 6);
    uint64_t failures = timed == NULL || rows == NULL || values == NULL || argv == NULL;
    for (uint32_t run = 0; failures == 0 && run < m->warmups + m->runs; run += 1)
    {
        const bool warmup = run < m->warmups;
        for (uint32_t position = 0; failures == 0 && position < compiles; position += 1)
        {
            const uint32_t c = warmup ? position : (uint32_t)profile_ab_tree(run - m->warmups, position, compiles);
            const ProfileTimed one = measure_compile_one(m, c, argv);
            const bool exited = one.status != -1 && WIFEXITED(one.status) && WEXITSTATUS(one.status) == 0;
            const uint64_t mark = arena->length;
            const char* errors = exited && c == 1 ? measure_compile_read(arena, m->errors) : NULL;
            double scratch[BUSTER_MEASURE_ROW] = { 0 };
            double* row = warmup ? scratch : rows + (uint64_t)(run - m->warmups) * BUSTER_MEASURE_ROW;
            const bool reported = c != 1 || (errors != NULL && check_clock_read(errors, row, row + CHECK_STAGE_COUNT, row + BUSTER_MEASURE_SPAN,
                                                                                 row + BUSTER_MEASURE_USAGE));
            if (c == 1)
            {
                row[BUSTER_MEASURE_OUTSIDE] = (double)one.wall_ns / 1e6 - row[BUSTER_MEASURE_SPAN];
            }
            arena_pop_to(arena, mark);
            if (!exited || !reported)
            {
                failures += 1;
                printf("%s %u %s: FAILED (%s)\n", warmup ? "warm-up" : "run", (warmup ? run : run - m->warmups) + 1, measure_compile_names[c],
                       !exited ? "the process failed" : "no whole --time-stages report");
            }
            if (!warmup)
            {
                timed[(uint64_t)(run - m->warmups) * BUSTER_MEASURE_COMPILES + c] = one;
                printf("run %u %s: %.3f ms wall, %.3f ms user, %.3f ms system, %llu KiB wait4 peak, %llu minor faults, %llu major\n", run - m->warmups + 1,
                       measure_compile_names[c], (double)one.wall_ns / 1e6, (double)one.user_us / 1e3, (double)one.system_us / 1e3,
                       (unsigned long long)one.peak_kb, (unsigned long long)one.minor_faults, (unsigned long long)one.major_faults);
            }
        }
        // The default's object must be the --time-stages one's, byte for byte.
        if (failures == 0 && !measure_compile_same(arena, m->outputs[0], m->outputs[1]))
        {
            failures += 1;
            printf("%s %u: FAILED (the default's object isn't --time-stages')\n", warmup ? "warm-up" : "run", (warmup ? run : run - m->warmups) + 1);
        }
    }
    double medians[BUSTER_MEASURE_COMPILES] = { 0 };
    for (uint32_t c = 0; failures == 0 && c < compiles; c += 1)
    {
        double fields[6];
        double low = 0;
        double high = 0;
        for (uint32_t field = 0; field < 6; field += 1)
        {
            for (uint32_t r = 0; r < m->runs; r += 1)
            {
                const ProfileTimed* one = &timed[(uint64_t)r * BUSTER_MEASURE_COMPILES + c];
                const double all[6] = { (double)one->wall_ns / 1e6, (double)one->user_us / 1e3, (double)one->system_us / 1e3, (double)one->peak_kb,
                                        (double)one->minor_faults, (double)one->major_faults };
                values[r] = all[field];
            }
            fields[field] = measure_compile_median(values, m->runs);
            low = field == 0 ? values[0] : low;
            high = field == 0 ? values[m->runs - 1] : high;
        }
        medians[c] = fields[0];
        printf("%s: median of %u: %.3f ms wall (%.3f to %.3f), %.3f ms user, %.3f ms system, %.0f KiB wait4 peak, %.0f minor faults, %.0f major\n",
               measure_compile_names[c], m->runs, fields[0], low, high, fields[1], fields[2], fields[3], fields[4], fields[5]);
    }
    if (failures == 0)
    {
        printf("--time-stages costs %.3f ms (%.2f%%) of the default's median wall time\n", medians[1] - medians[0],
               medians[0] > 0 ? (medians[1] - medians[0]) * 100.0 / medians[0] : 0.0);
    }
    if (failures == 0 && compiles == 3)
    {
        printf("default against reference: %.3f ms against %.3f ms, %.2f times, %.3f ms apart\n", medians[0], medians[2],
               medians[2] > 0 ? medians[0] / medians[2] : 0.0, medians[0] - medians[2]);
    }
    // Each column's median and range over the runs.
    double median[BUSTER_MEASURE_ROW] = { 0 };
    double low[BUSTER_MEASURE_ROW] = { 0 };
    double high[BUSTER_MEASURE_ROW] = { 0 };
    for (uint32_t i = 0; failures == 0 && i < BUSTER_MEASURE_ROW; i += 1)
    {
        const bool peak_column = i >= BUSTER_MEASURE_USAGE && (i - BUSTER_MEASURE_USAGE) % 3 == 2;
        bool unavailable = false;
        for (uint32_t r = 0; r < m->runs; r += 1)
        {
            values[r] = rows[(uint64_t)r * BUSTER_MEASURE_ROW + i];
            unavailable = unavailable || (peak_column && values[r] < 0);
        }
        // Do not sort unavailable sentinels into a plausible median of the remaining RSS readings.
        median[i] = unavailable ? -1 : measure_compile_median(values, m->runs);
        low[i] = unavailable ? -1 : values[0];
        high[i] = unavailable ? -1 : values[m->runs - 1];
    }
    if (failures == 0)
    {
        const double span = median[BUSTER_MEASURE_SPAN];
        double sum = 0;
        printf("--time-stages' stages, medians of %u runs: ms (range), share of the median span; system ms, minor faults, approximate post-exec peak KiB added (-1 unavailable)\n", m->runs);
        for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
        {
            const double* used = median + BUSTER_MEASURE_USAGE + 3 * s;
            sum += median[s];
            printf("  %-22s %9.3f  (%.3f to %.3f)  %5.1f%%  %8.3f  %7.0f  %8.0f\n", check_stage_names[s], median[s], low[s], high[s],
                   span > 0 ? median[s] * 100.0 / span : 0.0, used[0], used[1], used[2]);
            for (uint32_t p = 0; s == CHECK_STAGE_PREPROCESS && p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
            {
                const uint32_t index = CHECK_STAGE_COUNT + p;
                printf("    %-20s %9.3f  (%.3f to %.3f)  %5.1f%%\n", check_clock_phase_names[p], median[index], low[index], high[index],
                       span > 0 ? median[index] * 100.0 / span : 0.0);
            }
        }
        printf("  %-22s %9.3f  (%.3f to %.3f)  the stages' medians add up to %.3f\n", "span", span, low[BUSTER_MEASURE_SPAN], high[BUSTER_MEASURE_SPAN], sum);
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

static int run_measure_compile_command(int argc, char** argv)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    MeasureCompile m = { .runs = 11, .warmups = 1 };
    const char* parent = getenv("TMPDIR") != NULL && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool valid = arena.base != 0;
    int i = 1;
    while (valid && i < argc && strcmp(argv[i], "--") != 0)
    {
        uint64_t value = 0;
        if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc && profile_count_option(argv[i + 1], &value) && value <= BUSTER_MEASURE_MAX_RUNS)
        {
            m.runs = (uint32_t)value;
            i += 2;
        }
        else if (strcmp(argv[i], "--warmups") == 0 && i + 1 < argc && profile_decimal(profile_string(argv[i + 1]), &value) && value <= 1000)
        {
            m.warmups = (uint32_t)value;
            i += 2;
        }
        else if (strcmp(argv[i], "--reference") == 0 && i + 1 < argc)
        {
            m.reference = argv[i + 1];
            i += 2;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
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
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: measure-compile [--runs N] [--warmups N] [--reference PATH] [--directory DIR] [--keep] -- ARGUMENT...\n");
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        for (uint32_t c = 0; c < BUSTER_MEASURE_COMPILES; c += 1)
        {
            snprintf(m.outputs[c], sizeof(m.outputs[c]), "%s/%s.o", m.directory, measure_compile_names[c]);
        }
        snprintf(m.errors, sizeof(m.errors), "%s/errors.txt", m.directory);
        printf("measure-compile: %u warm-ups, %u runs, reference %s, in %s\n", m.warmups, m.runs, m.reference != NULL ? m.reference : "none", m.directory);
        result = measure_compile_run(&arena, &m) == 0 ? 0 : 1;
        if (!keep)
        {
            for (uint32_t c = 0; c < BUSTER_MEASURE_COMPILES; c += 1)
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
