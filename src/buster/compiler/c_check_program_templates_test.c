// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's node templates against the tasks' rules (c_check_program_templates.c), on any host:
//
//   build/compiler test-lowering-templates [--passes N] [preprocessor options] file...
//
// Each file is prepared as -c prepares it (lower_source_open), then its selected program is lowered by the tasks alone
// (check_program_lower's scalar path) and with the templates (check_program_lower_templates, which takes them on any
// host's path): both must succeed or fail alike, and every function must have the same entries, locals (their classes,
// flags, sizes and alignments), calls and arguments. The report gives, per file and in total, the evaluated nodes and the
// entries, and the share the templates' closed subtrees gave. A file the checker rejects is left out; any other failure
// or difference fails the command. With --passes N, each file's two lowerings are then timed N times each, in turns that
// alternate which goes first, and the report gives their medians (the arenas put back after each). test-checker runs the
// same comparison on original fixtures of every template (check_test_lowering_templates). Include after
// c_check_lower_inventory.c and c_check_source_benchmark.c.
#ifndef BUSTER_C_CHECK_PROGRAM_TEMPLATES_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TEMPLATES_TEST_INCLUDED

typedef struct LowerTemplatesTotals LowerTemplatesTotals;
struct LowerTemplatesTotals
{
    uint64_t files;
    uint64_t functions;
    uint64_t nodes;
    uint64_t entries;
    uint64_t template_nodes;
    uint64_t template_entries;
    uint64_t differences;
    uint64_t failures;
    uint64_t passes;
    uint64_t tasks_nanoseconds;
    uint64_t templates_nanoseconds;
};

// The medians of `passes` timed lowerings of `program` by the tasks and with the templates, alternating which goes first,
// each from the arenas as they were: whether every lowering succeeded.
static bool lower_templates_time(const CheckProgram* program, uint32_t passes, Arena* output, Arena* scratch, uint64_t* tasks_median, uint64_t* templates_median)
{
    uint64_t tasks[BUSTER_CHECK_SOURCE_MAX_PASSES];
    uint64_t templates[BUSTER_CHECK_SOURCE_MAX_PASSES];
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    bool valid = true;
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        for (uint32_t step = 0; valid && step < 2; step += 1)
        {
            const bool with_templates = ((pass + step) & 1) != 0;
            CheckProgramLowered lowered = { 0 };
            const uint64_t begin = check_source_time();
            valid = with_templates ? check_program_lower_templates(&lowered, program, output, scratch) : check_program_lower(&lowered, program, true, output, scratch);
            const uint64_t elapsed = check_source_time() - begin;
            if (with_templates)
            {
                templates[pass] = elapsed;
            }
            else
            {
                tasks[pass] = elapsed;
            }
            arena_pop_to(output, output_mark);
            arena_pop_to(scratch, scratch_mark);
        }
    }
    *tasks_median = valid ? check_source_median(tasks, passes) : 0;
    *templates_median = valid ? check_source_median(templates, passes) : 0;
    return valid;
}

// Whether two arrays of `count` items of `size` bytes are the same (both may be null when `count` is 0).
static bool lower_templates_same(const void* left, const void* right, uint64_t count, uint64_t size)
{
    return count == 0 || (left != NULL && right != NULL && memcmp(left, right, count * size) == 0);
}

// Prints function `b`'s first differing entry of `tasks` and `templates` with its neighbors.
static void lower_templates_print_difference(const char* name, uint32_t b, const Tier0Function* tasks, const Tier0Function* templates)
{
    const uint32_t count = tasks->count < templates->count ? tasks->count : templates->count;
    uint32_t e = 0;
    while (e < count && tasks->kinds[e] == templates->kinds[e] && tasks->classes[e] == templates->classes[e] && tasks->values[e] == templates->values[e])
    {
        e += 1;
    }
    printf("lowering-templates-test: %s function %u differs at entry %u (counts %u and %u, locals %u and %u)\n", name, b, e, tasks->count, templates->count,
           tasks->local_count, templates->local_count);
    for (uint32_t i = e >= 3 ? e - 3 : 0; i < e + 4; i += 1)
    {
        printf("  %u:", i);
        if (i < tasks->count)
        {
            printf(" tasks %s.%s %" PRIu64, tasks->kinds[i] < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[tasks->kinds[i]] : "?",
                   tasks->classes[i] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[tasks->classes[i]] : "?", tasks->values[i]);
        }
        if (i < templates->count)
        {
            printf("  templates %s.%s %" PRIu64, templates->kinds[i] < BUSTER_TYPED_KINDS ? lower_inventory_kind_names[templates->kinds[i]] : "?",
                   templates->classes[i] < BUSTER_TYPED_CLASS_LIMIT ? lower_inventory_class_names[templates->classes[i]] : "?", templates->values[i]);
        }
        printf("\n");
    }
}

// Lowers `program` both ways and compares them: whether both did as the other (succeeded alike, every function the same,
// or failed alike), with the counts in `totals`.
static bool lower_templates_compare(const char* name, const CheckProgram* program, Arena* output, Arena* scratch, LowerTemplatesTotals* totals)
{
    CheckProgramLowered tasks = { 0 };
    CheckProgramLowered templates = { 0 };
    const bool lowered_tasks = check_program_lower(&tasks, program, true, output, scratch);
    const bool lowered_templates = check_program_lower_templates(&templates, program, output, scratch);
    bool same = lowered_tasks == lowered_templates && tasks.status == templates.status && tasks.token == templates.token;
    for (uint32_t b = 0; same && lowered_tasks && b < program->body_count; b += 1)
    {
        const Tier0Function* a = tasks.functions + b;
        const Tier0Function* c = templates.functions + b;
        bool function = a->count == c->count && a->local_count == c->local_count && a->call_count == c->call_count && a->argument_count == c->argument_count &&
                        lower_templates_same(a->kinds, c->kinds, a->count, 1) && lower_templates_same(a->classes, c->classes, a->count, 1) &&
                        lower_templates_same(a->values, c->values, a->count, sizeof(uint64_t)) &&
                        lower_templates_same(a->local_classes, c->local_classes, a->local_count, 1) &&
                        lower_templates_same(a->local_flags, c->local_flags, a->local_count, 1) &&
                        lower_templates_same(a->call_targets, c->call_targets, a->call_count, sizeof(SymbolId)) &&
                        lower_templates_same(a->argument_classes, c->argument_classes, a->argument_count, 1);
        function = function && (a->local_sizes == NULL) == (c->local_sizes == NULL) &&
                   (a->local_sizes == NULL || (lower_templates_same(a->local_sizes, c->local_sizes, a->local_count, sizeof(uint32_t)) &&
                                               lower_templates_same(a->local_alignments, c->local_alignments, a->local_count, 1)));
        if (!function)
        {
            lower_templates_print_difference(name, b, a, c);
        }
        same = function;
    }
    if (lowered_tasks != lowered_templates || tasks.status != templates.status || tasks.token != templates.token)
    {
        printf("lowering-templates-test: %s lowered with status %u at token %u by the tasks and %u at %u with the templates\n", name, (uint32_t)tasks.status,
               tasks.token, (uint32_t)templates.status, templates.token);
    }
    totals->functions += program->body_count;
    totals->nodes += tasks.expression_nodes;
    totals->entries += tasks.entries;
    totals->template_nodes += templates.template_nodes;
    totals->template_entries += templates.template_entries;
    totals->differences += !same;
    return same;
}

// Compares file `file`, read with `options`: whether its options could be read.
static bool lower_templates_file(LowerTemplatesTotals* totals, const char* file, char** options, uint32_t option_count)
{
    const uint32_t passes = (uint32_t)totals->passes;
    static LowerSource source;
    const bool valid = lower_source_open(&source, file, options, option_count);
    if (valid && source.prepared)
    {
        const uint64_t nodes = totals->nodes;
        const uint64_t entries = totals->entries;
        const uint64_t template_nodes = totals->template_nodes;
        const uint64_t template_entries = totals->template_entries;
        const bool same = lower_templates_compare(file, &source.selected.program, &source.path.output_arena, &source.path.scratch, totals);
        totals->files += 1;
        const uint64_t file_nodes = totals->nodes - nodes;
        const uint64_t file_entries = totals->entries - entries;
        printf("lowering-templates-test: file=%s %s nodes=%" PRIu64 " templates=%" PRIu64 " (%.2f%%) entries=%" PRIu64 " template-entries=%" PRIu64 " (%.2f%%)\n",
               file, same ? "same" : "different", file_nodes, totals->template_nodes - template_nodes,
               file_nodes != 0 ? 100.0 * (double)(totals->template_nodes - template_nodes) / (double)file_nodes : 0.0, file_entries,
               totals->template_entries - template_entries,
               file_entries != 0 ? 100.0 * (double)(totals->template_entries - template_entries) / (double)file_entries : 0.0);
        uint64_t tasks_median = 0;
        uint64_t templates_median = 0;
        if (passes != 0 && lower_templates_time(&source.selected.program, passes, &source.path.output_arena, &source.path.scratch, &tasks_median, &templates_median))
        {
            totals->tasks_nanoseconds += tasks_median;
            totals->templates_nanoseconds += templates_median;
            printf("lowering-templates-test: file=%s passes=%u tasks_ns=%" PRIu64 " templates_ns=%" PRIu64 " ratio=%.3f\n", file, passes, tasks_median,
                   templates_median, tasks_median != 0 ? (double)templates_median / (double)tasks_median : 0.0);
        }
        else if (passes != 0)
        {
            totals->failures += 1;
            printf("lowering-templates-test: file=%s failed=\"a timed lowering failed\"\n", file);
        }
    }
    else if (valid && source.accepted)
    {
        totals->failures += 1;
        printf("lowering-templates-test: file=%s failed=\"%s\"\n", file, lower_source_failure(&source));
    }
    else if (valid)
    {
        totals->failures += !source.preprocessed;
        printf("lowering-templates-test: file=%s %s=\"%s\"\n", file, source.preprocessed ? "rejected" : "failed", lower_source_failure(&source));
    }
    lower_source_close(&source);
    return valid;
}

static void lower_templates_report(const LowerTemplatesTotals* totals)
{
    printf("lowering-templates-test: files=%" PRIu64 " functions=%" PRIu64 " nodes=%" PRIu64 " templates=%" PRIu64 " (%.2f%%) entries=%" PRIu64
           " template-entries=%" PRIu64 " (%.2f%%) differences=%" PRIu64 " failures=%" PRIu64 "\n",
           totals->files, totals->functions, totals->nodes, totals->template_nodes,
           totals->nodes != 0 ? 100.0 * (double)totals->template_nodes / (double)totals->nodes : 0.0, totals->entries, totals->template_entries,
           totals->entries != 0 ? 100.0 * (double)totals->template_entries / (double)totals->entries : 0.0, totals->differences, totals->failures);
    if (totals->passes != 0)
    {
        printf("lowering-templates-test: passes=%" PRIu64 " tasks_ns=%" PRIu64 " templates_ns=%" PRIu64 " ratio=%.3f\n", totals->passes, totals->tasks_nanoseconds,
               totals->templates_nanoseconds, totals->tasks_nanoseconds != 0 ? (double)totals->templates_nanoseconds / (double)totals->tasks_nanoseconds : 0.0);
    }
}

static int run_lowering_templates_tests(int count, char** arguments)
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
    LowerTemplatesTotals totals = { .passes = passes };
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        valid = lower_templates_file(&totals, files[i], options, option_count);
        fflush(stdout);
    }
    if (valid)
    {
        lower_templates_report(&totals);
    }
    else
    {
        fprintf(stderr, "usage: compiler test-lowering-templates [--passes N] [preprocessor options] file...\n");
    }
    free(files);
    free(options);
    return valid && totals.differences == 0 && totals.failures == 0 && totals.files != 0 ? 0 : 1;
}

// test-checker's section: original fixtures of every template, and of what they leave to the tasks around them,
// compared both ways; the templates must take some of their nodes.
static __attribute__((noinline)) void check_test_lowering_templates(CheckTest* test)
{
    static const char* const fixtures[] = {
        // Locals, globals, constants, conversions, members, pointers, subscripts, truth tests and stores.
        "struct point { int x; int y; long z; unsigned char c; double d; };\n"
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
        "    }\n"
        "    counter = counter + 1, n = -n;\n"
        "    (void)f(~n);\n"
        "    int* pointer = &counter;\n"
        "    *pointer = q.x + q.y % 7 + (int)q.z;\n"
        "    b = !b;\n"
        "    if (p && n > 3 && !(c & 4)) sum = sum / 3;\n"
        "    return i < n && c != 0 ? (int)sum + (int)d + b : -1;\n"
        "}\n"
        "int f(int a) { return a; }\n",
        // Addresses of locals and globals, pointers minus integers, unsigned and narrow arithmetic, compares of pointers.
        "struct node { struct node* next; short weight; unsigned flags; };\n"
        "static struct node nodes[8];\n"
        "unsigned walk(struct node* start, const char* text, unsigned long length)\n"
        "{\n"
        "    struct node local = { 0 };\n"
        "    struct node* cursor = start;\n"
        "    unsigned total = 0;\n"
        "    const char* end = text + length;\n"
        "    const char* back = end - 1;\n"
        "    local.next = &nodes[2];\n"
        "    while (cursor != 0 && cursor != &local)\n"
        "    {\n"
        "        total += (unsigned)cursor->weight * 3u + (cursor->flags << 1);\n"
        "        cursor = cursor->next;\n"
        "    }\n"
        "    total ^= (unsigned)(back - text) + (unsigned char)text[0] + (unsigned)(signed char)*back;\n"
        "    total = total > 100 ? total - 100 : total + local.flags;\n"
        "    return total + (unsigned)(&nodes[3] - nodes);\n"
        "}\n",
    };
    char directory[] = "/tmp/buster-lowering-templates-test-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    check_test_expect(test, ready, "lowering-templates test directory is made");
    LowerTemplatesTotals totals = { 0 };
    char* options[] = { "-nostdinc" };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        const bool written = parse_test_write(path, fixtures[i], strlen(fixtures[i]));
        const bool compared = written && lower_templates_file(&totals, path, options, BUSTER_ARRAY_LENGTH(options));
        check_test_expect(test, compared, "lowering-templates fixture is prepared");
        unlink(path);
    }
    check_test_expect(test, ready && totals.files == BUSTER_ARRAY_LENGTH(fixtures) && totals.failures == 0 && totals.differences == 0,
                      "node templates lower the fixtures as the tasks do, function for function");
    check_test_expect(test, totals.template_nodes * 2 > totals.nodes && totals.template_entries != 0,
                      "node templates take more than half of the fixtures' evaluated nodes");
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
