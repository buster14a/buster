// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's statement sweep against the tasks' loop (c_check_program_sweep.c), on a host with the vector path:
//
//   build/compiler test-lowering-sweep [--passes N] [preprocessor options] file...
//
// Each file is prepared as -c prepares it (lower_source_open), then its selected program is lowered by the vector path with
// the sweep (check_program_lower's) and without it (check_program_lower_unswept, the tasks' loop alone): both must succeed
// or fail alike, at the same token for the same reason, and every function must be the same: its entries, its locals
// (their classes, flags, sizes and alignments), its calls (their targets, arguments, rows' locations and result objects),
// its arguments' classes and sizes, its parameters, its result and its labels. The report gives, per file and in total,
// the evaluated nodes and the share the sweep lowered. A file the checker rejects is left out; any other failure or
// difference fails the command, and so do a file neither lowering lowers and a corpus with evaluated nodes the sweep took
// none of. With --passes N, each file's two lowerings are then timed N times each, in turns that alternate which goes
// first, and the report gives their medians. A host without the vector path has no sweep: the command says so and exits
// with status 77. test-checker runs the comparison on original fixtures (check_test_lowering_sweep). Include after
// c_check_program_templates_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_SWEEP_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_SWEEP_TEST_INCLUDED

typedef struct LowerSweepTotals LowerSweepTotals;
struct LowerSweepTotals
{
    uint64_t files;
    uint64_t functions;
    uint64_t nodes;
    uint64_t swept;
    uint64_t differences;
    uint64_t failures;
    uint64_t passes;
    uint64_t unswept_nanoseconds;
    uint64_t swept_nanoseconds;
};

// The medians of `passes` timed lowerings of `program` without the sweep and with it, alternating which goes first, each
// from the arenas as they were: whether every lowering succeeded.
static bool lower_sweep_time(const CheckProgram* program, uint32_t passes, Arena* output, Arena* scratch, uint64_t* unswept_median, uint64_t* swept_median)
{
    uint64_t unswept[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t swept[BUSTER_CHECK_SOURCE_MAX_PASSES];
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    bool valid = true;
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 2; step += 1)
        {
            const bool with_sweep = ((pass + step) & 1) != 0;
            CheckProgramLowered lowered = { 0 };
            const uint64_t begin = check_source_time();
            valid = with_sweep ? check_program_lower(&lowered, program, false, output, scratch) : check_program_lower_unswept(&lowered, program, output, scratch);
            const uint64_t elapsed = check_source_time() - begin;
            if (with_sweep)
            {
                swept[pass] = elapsed;
            }
            else
            {
                unswept[pass] = elapsed;
            }
            arena_pop_to(output, output_mark);
            arena_pop_to(scratch, scratch_mark);
        }
    }
    *unswept_median = valid ? check_source_median(unswept, passes) : 0;
    *swept_median = valid ? check_source_median(swept, passes) : 0;
    return valid;
}

// Whether two optional columns of `count` items of `size` bytes are the same: both absent, or both present and equal.
static bool lower_sweep_same(const void* left, const void* right, uint64_t count, uint64_t size)
{
    return count == 0 || (left == NULL && right == NULL) || (left != NULL && right != NULL && memcmp(left, right, count * size) == 0);
}

// Whether function `a` (the tasks') and `c` (the sweep's) are the same, every column the emission reads.
static bool lower_sweep_function_same(const Tier0Function* a, const Tier0Function* c)
{
    bool same = a->count == c->count && a->local_count == c->local_count && a->call_count == c->call_count && a->argument_count == c->argument_count &&
                a->parameter_count == c->parameter_count && a->label_count == c->label_count && a->table_count == c->table_count &&
                a->lifetime_count == c->lifetime_count && a->x87 == c->x87 && a->int128 == c->int128 && a->vectors == c->vectors &&
                a->result_size == c->result_size && memcmp(&a->result, &c->result, sizeof(a->result)) == 0;
    same = same && lower_sweep_same(a->kinds, c->kinds, a->count, 1) && lower_sweep_same(a->classes, c->classes, a->count, 1) &&
           lower_sweep_same(a->values, c->values, a->count, sizeof(uint64_t));
    same = same && lower_sweep_same(a->local_classes, c->local_classes, a->local_count, 1) && lower_sweep_same(a->local_flags, c->local_flags, a->local_count, 1) &&
           lower_sweep_same(a->local_sizes, c->local_sizes, a->local_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->local_alignments, c->local_alignments, a->local_count, 1);
    same = same && lower_sweep_same(a->call_targets, c->call_targets, a->call_count, sizeof(SymbolId)) &&
           lower_sweep_same(a->call_first, c->call_first, a->call_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->call_argument_counts, c->call_argument_counts, a->call_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->call_variadic, c->call_variadic, a->call_count, 1) && lower_sweep_same(a->calls, c->calls, a->call_count, sizeof(AbiCall)) &&
           lower_sweep_same(a->call_result_objects, c->call_result_objects, a->call_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->call_tails, c->call_tails, a->call_count, 1);
    same = same && lower_sweep_same(a->argument_classes, c->argument_classes, a->argument_count, 1) &&
           lower_sweep_same(a->argument_sizes, c->argument_sizes, a->argument_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->argument_locations, c->argument_locations, a->argument_count, sizeof(AbiLocation)) &&
           lower_sweep_same(a->parameters, c->parameters, a->parameter_count, sizeof(AbiLocation));
    same = same && lower_sweep_same(a->label_entries, c->label_entries, a->label_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->label_tables, c->label_tables, a->label_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->lifetime_entries, c->lifetime_entries, a->lifetime_count, sizeof(uint32_t)) &&
           lower_sweep_same(a->lifetime_locals, c->lifetime_locals, a->lifetime_count, sizeof(uint32_t));
    return same;
}

// Prints function `b`'s first differing entry of the tasks' and the sweep's lowerings with its neighbors, and its counts.
static void lower_sweep_print_difference(const char* name, uint32_t b, const Tier0Function* tasks, const Tier0Function* swept)
{
    const uint32_t count = tasks->count < swept->count ? tasks->count : swept->count;
    uint32_t e = 0;
    while (e < count && tasks->kinds[e] == swept->kinds[e] && tasks->classes[e] == swept->classes[e] && tasks->values[e] == swept->values[e])
    {
        e += 1;
    }
    printf("lowering-sweep-test: %s function %u differs at entry %u (counts %u and %u, locals %u and %u, calls %u and %u, arguments %u and %u)\n", name, b, e,
           tasks->count, swept->count, tasks->local_count, swept->local_count, tasks->call_count, swept->call_count, tasks->argument_count, swept->argument_count);
    for (uint32_t i = e >= 3 ? e - 3 : 0; i < e + 4; i += 1)
    {
        printf("  %u:", i);
        if (i < tasks->count)
        {
            printf(" tasks %s.%s %" PRIu64, tasks->kinds[i] < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[tasks->kinds[i]] : "?",
                   tasks->classes[i] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[tasks->classes[i]] : "?", tasks->values[i]);
        }
        if (i < swept->count)
        {
            printf("  sweep %s.%s %" PRIu64, swept->kinds[i] < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[swept->kinds[i]] : "?",
                   swept->classes[i] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[swept->classes[i]] : "?", swept->values[i]);
        }
        printf("\n");
    }
}

// Lowers `program` both ways and compares them: whether both did as the other, with the counts in `totals`.
static bool lower_sweep_compare(const char* name, const CheckProgram* program, Arena* output, Arena* scratch, LowerSweepTotals* totals)
{
    CheckProgramLowered tasks = { 0 };
    CheckProgramLowered swept = { 0 };
    const bool lowered_tasks = check_program_lower_unswept(&tasks, program, output, scratch);
    const bool lowered_swept = check_program_lower(&swept, program, false, output, scratch);
    const bool reasons = tasks.reason == swept.reason || (tasks.reason != NULL && swept.reason != NULL && strcmp(tasks.reason, swept.reason) == 0);
    bool same = lowered_tasks == lowered_swept && tasks.status == swept.status && tasks.token == swept.token && reasons && tasks.entries == swept.entries &&
                tasks.expression_nodes == swept.expression_nodes;
    for (uint32_t b = 0; same && lowered_tasks && b < program->body_count; b += 1)
    {
        const bool function = lower_sweep_function_same(tasks.functions + b, swept.functions + b);
        if (!function)
        {
            lower_sweep_print_difference(name, b, tasks.functions + b, swept.functions + b);
        }
        same = function;
    }
    if (lowered_tasks != lowered_swept || tasks.status != swept.status || tasks.token != swept.token || !reasons)
    {
        printf("lowering-sweep-test: %s lowered with status %u at token %u (\"%s\") by the tasks and %u at %u (\"%s\") with the sweep\n", name, (uint32_t)tasks.status,
               tasks.token, tasks.reason != NULL ? tasks.reason : "", (uint32_t)swept.status, swept.token, swept.reason != NULL ? swept.reason : "");
    }
    else if (tasks.entries != swept.entries || tasks.expression_nodes != swept.expression_nodes)
    {
        printf("lowering-sweep-test: %s entries %" PRIu64 " and %" PRIu64 ", evaluated nodes %" PRIu64 " and %" PRIu64 "\n", name, tasks.entries, swept.entries,
               tasks.expression_nodes, swept.expression_nodes);
    }
    else if (!lowered_tasks)
    {
        // Both refused it alike: the file proves nothing, which fails the command as any other failure does.
        printf("lowering-sweep-test: %s failed=\"neither lowering succeeded: status %u at token %u (%s)\"\n", name, (uint32_t)tasks.status, tasks.token,
               tasks.reason != NULL ? tasks.reason : "no reason");
        totals->failures += 1;
    }
    totals->functions += program->body_count;
    totals->nodes += tasks.expression_nodes;
    totals->swept += swept.sweep_nodes;
    totals->differences += !same;
    return same;
}

// Compares file `file`, read with `options`: whether its options could be read.
static bool lower_sweep_file(LowerSweepTotals* totals, const char* file, char** options, uint32_t option_count)
{
    const uint32_t passes = (uint32_t)totals->passes;
    static LowerSource source;
    const bool valid = lower_source_open(&source, file, options, option_count);
    if (valid && source.prepared)
    {
        const uint64_t nodes = totals->nodes;
        const uint64_t swept = totals->swept;
        const bool same = lower_sweep_compare(file, &source.selected.program, &source.path.output_arena, &source.path.scratch, totals);
        totals->files += 1;
        const uint64_t file_nodes = totals->nodes - nodes;
        printf("lowering-sweep-test: file=%s %s nodes=%" PRIu64 " swept=%" PRIu64 " (%.2f%%)\n", file, same ? "same" : "different", file_nodes, totals->swept - swept,
               file_nodes != 0 ? 100.0 * (double)(totals->swept - swept) / (double)file_nodes : 0.0);
        uint64_t unswept_median = 0;
        uint64_t swept_median = 0;
        if (passes != 0 && lower_sweep_time(&source.selected.program, passes, &source.path.output_arena, &source.path.scratch, &unswept_median, &swept_median))
        {
            totals->unswept_nanoseconds += unswept_median;
            totals->swept_nanoseconds += swept_median;
            printf("lowering-sweep-test: file=%s passes=%u tasks_ns=%" PRIu64 " sweep_ns=%" PRIu64 " ratio=%.3f\n", file, passes, unswept_median, swept_median,
                   unswept_median != 0 ? (double)swept_median / (double)unswept_median : 0.0);
        }
        else if (passes != 0)
        {
            totals->failures += 1;
            printf("lowering-sweep-test: file=%s failed=\"a timed lowering failed\"\n", file);
        }
    }
    else if (valid && source.accepted)
    {
        totals->failures += 1;
        printf("lowering-sweep-test: file=%s failed=\"%s\"\n", file, lower_source_failure(&source));
    }
    else if (valid)
    {
        totals->failures += !source.preprocessed || source.exhausted;
        printf("lowering-sweep-test: file=%s %s=\"%s\"\n", file, source.preprocessed && !source.exhausted ? "rejected" : "failed", lower_source_failure(&source));
    }
    lower_source_close(&source);
    return valid;
}

static void lower_sweep_report(const LowerSweepTotals* totals)
{
    printf("lowering-sweep-test: files=%" PRIu64 " functions=%" PRIu64 " nodes=%" PRIu64 " swept=%" PRIu64 " (%.2f%%) differences=%" PRIu64 " failures=%" PRIu64 "\n",
           totals->files, totals->functions, totals->nodes, totals->swept, totals->nodes != 0 ? 100.0 * (double)totals->swept / (double)totals->nodes : 0.0,
           totals->differences, totals->failures);
    if (totals->passes != 0)
    {
        printf("lowering-sweep-test: passes=%" PRIu64 " tasks_ns=%" PRIu64 " sweep_ns=%" PRIu64 " ratio=%.3f\n", totals->passes, totals->unswept_nanoseconds,
               totals->swept_nanoseconds, totals->unswept_nanoseconds != 0 ? (double)totals->swept_nanoseconds / (double)totals->unswept_nanoseconds : 0.0);
    }
}

static int run_lowering_sweep_tests(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    uint64_t passes = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--passes") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &passes) && passes <= BUSTER_CHECK_SOURCE_MAX_PASSES;
        }
        else if (strcmp(arguments[i], "--no-default-config") == 0)
        {
            // Clang's option, which the corpora's command lines take: nothing for pp_parse_options to read.
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else
        {
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: compiler test-lowering-sweep [--passes N] [preprocessor options] file...\n");
    }
    else if (BUSTER_SCALAR_PATH(false))
    {
        // A host without the vector path lowers by the tasks alone, which the sweep is the vector path's alternative to.
        printf("lowering-sweep-test: skipped=\"this host has no vector path, whose lowering the sweep is\"\n");
        result = 77;
    }
    else
    {
        LowerSweepTotals totals = { .passes = passes };
        for (uint32_t i = 0; valid && i < file_count; i += 1)
        {
            valid = lower_sweep_file(&totals, files[i], options, option_count);
            fflush(stdout);
        }
        // A corpus whose evaluated nodes the sweep took none of proves nothing about it.
        const bool taken = totals.nodes == 0 || totals.swept != 0;
        if (valid)
        {
            lower_sweep_report(&totals);
            if (!taken)
            {
                printf("lowering-sweep-test: failed=\"the sweep took none of the %" PRIu64 " evaluated nodes\"\n", totals.nodes);
            }
        }
        result = valid && totals.differences == 0 && totals.failures == 0 && totals.files != 0 && taken ? 0 : 1;
    }
    free(files);
    free(options);
    return result;
}

// test-checker's section: original fixtures of every shape the sweep takes, and of the task nodes, calls and their rows
// around them, compared both ways where the host has the vector path; the sweep must take most of their nodes.
static __attribute__((noinline)) void check_test_lowering_sweep(CheckTest* test)
{
    static const char* const fixtures[] = {
        // Locals, globals, constants, conversions, members, pointers, subscripts, truth tests and stores, with task nodes
        // (updates, logical operators, bit-fields, aggregates) among them.
        "struct point { int x; int y; long z; unsigned char c; double d; unsigned bits : 3; };\n"
        "static int counter;\n"
        "long table[16];\n"
        "int f(int a);\n"
        "int g(struct point* p, int n, unsigned char c, struct point q)\n"
        "{\n"
        "    int i = 0;\n"
        "    long sum = 0;\n"
        "    double d = 1.5;\n"
        "    float h = 2.0f;\n"
        "    _Bool b = n;\n"
        "    for (i = 0; i < n; i++)\n"
        "    {\n"
        "        sum += p[i].x + p->y * c - table[i & 15];\n"
        "        d = d * p[i].d + h;\n"
        "        table[i & 15] = sum >> 2;\n"
        "        p[i].c = (unsigned char)(c ^ i);\n"
        "        p[i].bits = i;\n"
        "    }\n"
        "    counter = counter + 1, n = -n;\n"
        "    (void)f(~n);\n"
        "    int* pointer = &counter;\n"
        "    *pointer = q.x + q.y % 7 + (int)q.z;\n"
        "    b = !b;\n"
        "    if (p && n > 3 && !(c & 4)) sum = sum / 3;\n"
        "    unsigned u = (unsigned)n / 8u + (unsigned)n % 16u;\n"
        "    return i < n && c != 0 ? (int)sum + (int)d + b + (int)u : -1;\n"
        "}\n"
        "int f(int a) { return a; }\n",
        // Calls nested in calls, beside each other and in task nodes, indirect calls, variadic calls and calls of no
        // prototype, whose rows the sweep places at the tasks' preorder's places.
        "int printf(const char*, ...);\n"
        "int h();\n"
        "static int add(int a, int b) { return a + b; }\n"
        "static long twice(long a) { return a * 2; }\n"
        "static int (*pick)(int, int) = add;\n"
        "struct pair { int a; int b; };\n"
        "static struct pair make(int a) { struct pair p = { a, a }; return p; }\n"
        "int calls(int x, int y, int* q)\n"
        "{\n"
        "    int r = add(add(x, y), add(y, x));\n"
        "    r += add(x, 1) * add(2, y) + (int)twice(add(r, 3));\n"
        "    r = pick(add(r, x), pick(y, 2));\n"
        "    printf(\"%d %d %ld\\n\", r, add(x, *q), twice(r));\n"
        "    q[add(1, 2)] = add(x, y) + make(add(x, y)).a;\n"
        "    r = add(x > 0 && y > 0 ? add(x, y) : 0, h(x, y));\n"
        "    if (add(x, y) > twice(add(y, x))) r = r - add(r, 1);\n"
        "    return add(r, (int)twice((long)add(x, y)));\n"
        "}\n",
    };
    if (!BUSTER_SCALAR_PATH(false))
    {
        char directory[] = "/tmp/buster-lowering-sweep-test-XXXXXX";
        bool ready = mkdtemp(directory) != 0;
        check_test_expect(test, ready, "lowering-sweep test directory is made");
        LowerSweepTotals totals = { 0 };
        char* options[] = { "-nostdinc" };
        for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            char path[256];
            snprintf(path, sizeof(path), "%s/%u.c", directory, i);
            const bool written = parse_test_write(path, fixtures[i], strlen(fixtures[i]));
            const bool compared = written && lower_sweep_file(&totals, path, options, BUSTER_ARRAY_LENGTH(options));
            check_test_expect(test, compared, "lowering-sweep fixture is prepared");
            unlink(path);
        }
        check_test_expect(test, ready && totals.files == BUSTER_ARRAY_LENGTH(fixtures) && totals.failures == 0 && totals.differences == 0,
                          "the statement sweep lowers the fixtures as the tasks do, function for function");
        check_test_expect(test, totals.swept * 2 > totals.nodes, "the statement sweep takes more than half of the fixtures' evaluated nodes");
        if (ready)
        {
            rmdir(directory);
        }
    }
}

#endif
