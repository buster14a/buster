from pathlib import Path
import hashlib

expected = {
    'tools/throughput/platform.h': '048b6b10d382fbbd702cbb8e740d31e396d7d49c',
    'tools/throughput/throughput.c': 'c976b720d53ded661f1c9b1e825607226e215df1',
    'tools/throughput/tests.c': 'f170f7ed3c475a0dfbb071d1923969490b0bfd03',
    'tools/throughput/README.md': 'f54dc096bffe6a93febd33ceed4ea177732bc1f1',
    '.github/workflows/compiler-throughput.yml': '1312e531386cbb4ed9a96a68dfe27abcbc2710b6',
}
texts = {}
for name, wanted in expected.items():
    data = Path(name).read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != wanted:
        raise SystemExit(f'Unexpected baseline blob for {name}: {actual}')
    texts[name] = data.decode('utf-8')

def replace(name, old, new, count=1):
    actual = texts[name].count(old)
    if actual != count:
        raise SystemExit(f'Anchor count for {name}: expected {count}, got {actual}: {old!r}')
    texts[name] = texts[name].replace(old, new)

p = 'tools/throughput/platform.h'
replace(p, ' * mark (not a sum of simultaneously live process-tree RSS).\n', ''' * mark (not a sum of simultaneously live process-tree RSS). Linux also retains
 * wait4 page-fault and context-switch counts after the wall interval ends.
 * Their availability bits distinguish an observed zero from an unsupported
 * platform or failed wait. They are diagnostics, never PMU events or gates.
''')
replace(p, 'typedef struct TpProcess\n', '''typedef enum TpDiagnostic
{
    TP_MINOR_FAULTS,
    TP_MAJOR_FAULTS,
    TP_VOLUNTARY_SWITCHES,
    TP_INVOLUNTARY_SWITCHES,
    TP_DIAGNOSTICS
} TpDiagnostic;

typedef struct TpProcess
''')
replace(p, '    int counter_errors[TP_COUNTERS];\n', '''    int counter_errors[TP_COUNTERS];
    uint64_t diagnostics[TP_DIAGNOSTICS];
    unsigned diagnostics_available;
''')
replace(p, '#ifdef __APPLE__\n            result.peak_rss_bytes', '''#ifdef __linux__
            long const diagnostics[TP_DIAGNOSTICS] = {
                [TP_MINOR_FAULTS] = usage.ru_minflt,
                [TP_MAJOR_FAULTS] = usage.ru_majflt,
                [TP_VOLUNTARY_SWITCHES] = usage.ru_nvcsw,
                [TP_INVOLUNTARY_SWITCHES] = usage.ru_nivcsw};
            for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i)
            {
                if (diagnostics[i] >= 0)
                {
                    result.diagnostics[i] = (uint64_t)diagnostics[i];
                    result.diagnostics_available |= 1u << i;
                }
            }
#endif
#ifdef __APPLE__
            result.peak_rss_bytes''')

p = 'tools/throughput/throughput.c'
replace(p, '#define TP_SCHEMA 1\n', '''#define TP_SCHEMA 2
/* Result columns must not change the generated workload's byte identity. */
#define TP_INPUT_SCHEMA 1
#define TP_RAW_FIELDS (22 + TP_DIAGNOSTICS)
#define TP_STANDARD_METRICS 15
#define TP_METRICS (TP_STANDARD_METRICS + TP_DIAGNOSTICS)
''')
replace(p, 'source_functions,output_sha256\\n"', 'source_functions,output_sha256,minor_faults,major_faults,voluntary_context_switches,involuntary_context_switches\\n"')
replace(p, '                    TP_SCHEMA, config->seed, config->profile, config->scale, workload->name);',
           '                    TP_INPUT_SCHEMA, config->seed, config->profile, config->scale, workload->name);')
replace(p, r'''    fprintf(file, ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
            row->output_bytes, row->source_bytes, row->source_lines, row->source_functions, row->output_hash);''', r'''    fprintf(file, ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s",
            row->output_bytes, row->source_bytes, row->source_lines, row->source_functions, row->output_hash);
    for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i)
    {
        if (process->diagnostics_available & (1u << i)) fprintf(file, ",%" PRIu64, process->diagnostics[i]);
        else fputs(",NA", file);
    }
    fputc('\n', file);''')
replace(p, r'''        fputs("\"allocation_scope\":\"separate explicitly instrumented compiler replay; arena calls and requested bytes\",", file);''', r'''        fputs("\"allocation_scope\":\"separate explicitly instrumented compiler replay; arena calls and requested bytes\",", file);
        fprintf(file, "\"input_schema\":%d,", TP_INPUT_SCHEMA);
        fputs("\"process_diagnostics_scope\":\"Linux wait4 child usage; faults and context switches, not PMU events; copied after timing; other platforms unavailable; diagnostic only\",", file);''')
replace(p, '            char* fields[22];', '            char* fields[TP_RAW_FIELDS];')
replace(p, 'if (count == 22)', 'if (count == TP_RAW_FIELDS)')
replace(p, 'if (!ok || count != 22)', 'if (!ok || count != TP_RAW_FIELDS)')
replace(p, '            for (unsigned i = 0; i < 64 && ok; ++i)\n', '''            row->process.diagnostics_available = 0;
            for (unsigned i = 0; i < TP_DIAGNOSTICS && ok; ++i)
            {
                row->process.diagnostics[i] = 0;
                if (strcmp(fields[22 + i], "NA"))
                {
                    ok = tp_parse_u64(fields[22 + i], &row->process.diagnostics[i]);
                    if (ok) row->process.diagnostics_available |= 1u << i;
                }
            }
            for (unsigned i = 0; i < 64 && ok; ++i)
''')
replace(p, '    else if (metric == 14) result = (double)row->source_bytes / row->process.wall_seconds;\n', '''    else if (metric == 14) result = (double)row->source_bytes / row->process.wall_seconds;
    else if (metric >= TP_STANDARD_METRICS && metric < TP_METRICS)
    {
        unsigned diagnostic = metric - TP_STANDARD_METRICS;
        if (row->process.diagnostics_available & (1u << diagnostic))
            result = (double)row->process.diagnostics[diagnostic];
    }
''')
replace(p, '        fclose(config);\n', '''        if (schema && schema != TP_SCHEMA)
            tp_error("unsupported result schema %u (expected %d); retain the old harness to replay that bundle", schema, TP_SCHEMA);
        fclose(config);
''')
replace(p, 'metric_names[15]', 'metric_names[TP_METRICS]')
replace(p, '"functions_per_second", "bytes_per_second"};', '''"functions_per_second", "bytes_per_second",
            "minor_faults", "major_faults", "voluntary_context_switches", "involuntary_context_switches"};''')
replace(p, 'double medians[2][15];', 'double medians[2][TP_METRICS];')
replace(p, 'for (unsigned metric = 0; metric < 15; ++metric)', 'for (unsigned metric = 0; metric < TP_METRICS; ++metric)')
replace(p, '''                for (unsigned metric = begin; metric < end; ++metric)
                {
                    if (metric != begin) fputc(',', json);''', '''                unsigned ordinary = end - begin;
                for (unsigned item = 0; item < ordinary + TP_DIAGNOSTICS; ++item)
                {
                    unsigned metric = item < ordinary ? begin + item : TP_STANDARD_METRICS + item - ordinary;
                    if (item) fputc(',', json);''')
replace(p, '                        if (kind && count != 3) ok = 0;', '                        if (kind && metric < TP_STANDARD_METRICS && count != 3) ok = 0;')
replace(p, '                        if (count) { tp_sort(values, count); tp_json_number(json, tp_median_sorted(values, count)); }',
           '                        if (count && (metric < TP_STANDARD_METRICS || count == 3)) { tp_sort(values, count); tp_json_number(json, tp_median_sorted(values, count)); }')
replace(p, 'Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials.',
           'OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials.')
replace(p, 'Compiler throughput (native C; schema 1)', 'Compiler throughput (native C; result schema 2, input schema 1)')

p = 'tools/throughput/tests.c'
replace(p, '                    row.process.peak_rss_bytes = 64.0 * 1048576.0;\n', '''                    row.process.peak_rss_bytes = 64.0 * 1048576.0;
                    if (scenario == 10 || scenario == 11)
                    {
                        row.process.diagnostics_available = (1u << TP_DIAGNOSTICS) - 1u;
                        for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i)
                            row.process.diagnostics[i] = scenario == 10 ? (variant ? UINT64_C(1000000000000) : 1) : 0;
                        if (scenario == 11 && variant && pair == 3)
                            row.process.diagnostics_available &= ~(1u << TP_MINOR_FAULTS);
                    }
''')
replace(p, '    else if (!strcmp(argv[2], "echo"))\n', '''    else if (!strcmp(argv[2], "compare") && argc == 4) result = tp_compare(argv[3]);
    else if (!strcmp(argv[2], "echo"))
''')
replace(p, '    TpProcess large = tp_process(memory, NULL, log, 3, -1, 0);\n', '''#ifdef __linux__
    struct rusage before = {0}, after = {0};
    CHECK(getrusage(RUSAGE_CHILDREN, &before) == 0);
#endif
    TpProcess large = tp_process(memory, NULL, log, 3, -1, 0);
#ifdef __linux__
    CHECK(getrusage(RUSAGE_CHILDREN, &after) == 0);
    /* No other child runs between these snapshots. The independent cumulative
     * OS delta must match this invocation's wait4 result, not lifetime totals. */
    CHECK(large.diagnostics_available == (1u << TP_DIAGNOSTICS) - 1u);
    CHECK(large.diagnostics[TP_MINOR_FAULTS] == (uint64_t)(after.ru_minflt - before.ru_minflt));
    CHECK(large.diagnostics[TP_MAJOR_FAULTS] == (uint64_t)(after.ru_majflt - before.ru_majflt));
    CHECK(large.diagnostics[TP_VOLUNTARY_SWITCHES] == (uint64_t)(after.ru_nvcsw - before.ru_nvcsw));
    CHECK(large.diagnostics[TP_INVOLUNTARY_SWITCHES] == (uint64_t)(after.ru_nivcsw - before.ru_nivcsw));
    CHECK(large.diagnostics[TP_MINOR_FAULTS] > 0 && large.diagnostics[TP_VOLUNTARY_SWITCHES] > 0);
    printf("PROCESS_DIAGNOSTICS minor=%" PRIu64 " major=%" PRIu64 " voluntary=%" PRIu64 " involuntary=%" PRIu64 "\\n",
           large.diagnostics[TP_MINOR_FAULTS], large.diagnostics[TP_MAJOR_FAULTS],
           large.diagnostics[TP_VOLUNTARY_SWITCHES], large.diagnostics[TP_INVOLUNTARY_SWITCHES]);
#else
    CHECK(large.diagnostics_available == 0 && timeout.diagnostics_available == 0 && failed.diagnostics_available == 0);
#endif
    TpProcess rejected = tp_process(fail, NULL, root, 2, -1, 0);
    CHECK(rejected.launch_error && rejected.diagnostics_available == 0);
''')
replace(p, 'static void test_compile_options(void)\n', r'''/* Exact integer serialization is independent of floating-point summary
 * medians. Zero, UINT64_MAX and unavailable must survive a complete replay. */
static void test_process_fields(char const* root)
{
    char path[TP_PATH_CAP];
    int path_ok = tp_path(path, root, "process-fields.csv");
    CHECK(path_ok);
    TpRow row = {0};
    row.process.wall_seconds = 1.0;
    row.process.user_seconds = 0.8;
    row.process.system_seconds = 0.1;
    row.process.peak_rss_bytes = 67108864.0;
    row.process.diagnostics_available = (1u << TP_MINOR_FAULTS) | (1u << TP_MAJOR_FAULTS) | (1u << TP_VOLUNTARY_SWITCHES);
    row.process.diagnostics[TP_MAJOR_FAULTS] = UINT64_MAX;
    row.process.diagnostics[TP_VOLUNTARY_SWITCHES] = 17;
    for (unsigned i = 0; i < TP_COUNTERS; ++i) row.process.counters[i] = NAN;
    row.arena_calls = row.arena_bytes = NAN;
    row.output_bytes = 4; row.source_bytes = 16; row.source_lines = 2; row.source_functions = 1;
    memset(row.output_hash, '0', 64); row.output_hash[64] = 0;
    FILE* file = path_ok ? fopen(path, "wb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        fputs(TP_RAW_HEADER, file);
        for (unsigned round = 0; round < TP_ROUNDS; ++round)
            for (unsigned variant = 0; variant < 2; ++variant)
                CHECK(tp_sample_csv(file, round, 0, variant, variant, 0, &row));
        CHECK(fclose(file) == 0);
        TpRow loaded[TP_ROUNDS * 2] = {0};
        CHECK(tp_load_samples(root, "process-fields.csv", 1, 1, 1, loaded));
        for (unsigned i = 0; i < TP_ROUNDS * 2; ++i)
        {
            CHECK(loaded[i].process.diagnostics_available == row.process.diagnostics_available);
            CHECK(loaded[i].process.diagnostics[TP_MINOR_FAULTS] == 0);
            CHECK(loaded[i].process.diagnostics[TP_MAJOR_FAULTS] == UINT64_MAX);
            CHECK(loaded[i].process.diagnostics[TP_VOLUNTARY_SWITCHES] == 17);
            CHECK(isnan(tp_metric(&loaded[i], TP_STANDARD_METRICS + TP_INVOLUNTARY_SWITCHES)));
        }
    }
    static char const* const tokens[] = {"NA", "0", "18446744073709551615", "-1", "1.5", "nan", "inf", "", "18446744073709551616", "1e3", "0x1"};
    for (unsigned token = 0; token < sizeof(tokens) / sizeof(tokens[0]); ++token)
    {
        file = path_ok ? fopen(path, "wb") : NULL;
        CHECK(file != NULL);
        if (file)
        {
            fputs(TP_RAW_HEADER, file);
            for (unsigned round = 0; round < TP_ROUNDS; ++round)
            {
                for (unsigned variant = 0; variant < 2; ++variant)
                    fprintf(file, "%u,0,%u,%u,0,1,0.8,0.1,67108864,NA,NA,NA,NA,NA,NA,NA,NA,4,16,2,1,%s,%s,0,0,0\n",
                            round, variant, variant, row.output_hash, tokens[token]);
            }
            CHECK(fclose(file) == 0);
            TpRow loaded[TP_ROUNDS * 2] = {0};
            CHECK(tp_load_samples(root, "process-fields.csv", 1, 1, 1, loaded) == (token < 3));
        }
    }
}

static void test_diagnostic_probes(char const* root)
{
    char directory[TP_PATH_CAP], path[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "diagnostic-probes") && test_bundle(directory, 0) &&
                   tp_path(path, directory, "telemetry.csv");
    CHECK(paths_ok);
    FILE* file = paths_ok ? fopen(path, "wb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        fputs(TP_RAW_HEADER, file);
        for (unsigned kind = 0; kind < 2; ++kind)
        {
            for (unsigned repeat = 0; repeat < 3; ++repeat)
            {
                for (unsigned variant = 0; variant < 2; ++variant)
                {
                    TpRow row = {0};
                    row.process.wall_seconds = 1.0;
                    row.process.peak_rss_bytes = 67108864.0;
                    for (unsigned i = 0; i < TP_COUNTERS; ++i) row.process.counters[i] = NAN;
                    row.arena_calls = kind ? 3.0 : NAN;
                    row.arena_bytes = kind ? 15.0 : NAN;
                    row.output_bytes = 4; row.source_bytes = 16; row.source_lines = 2; row.source_functions = 1;
                    memset(row.output_hash, '0', 64); row.output_hash[64] = 0;
                    if (!kind)
                    {
                        row.process.diagnostics_available = (1u << TP_DIAGNOSTICS) - 1u;
                        for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i) row.process.diagnostics[i] = repeat + variant;
                        if (variant && repeat == 2) row.process.diagnostics_available &= ~(1u << TP_MINOR_FAULTS);
                    }
                    CHECK(tp_sample_csv(file, kind, repeat, variant, variant, 0, &row));
                }
            }
        }
        CHECK(fclose(file) == 0);
        CHECK(tp_completion(directory, 1, 20, 1, 1));
        CHECK(tp_compare(directory) == 0);
        paths_ok = tp_path(path, directory, "summary.json");
        CHECK(paths_ok);
        file = paths_ok ? fopen(path, "rb") : NULL;
        CHECK(file != NULL);
        if (file)
        {
            char text[16384] = {0};
            size_t size = fread(text, 1, sizeof(text) - 1, file);
            CHECK(size < sizeof(text) - 1 && !ferror(file));
            CHECK(strstr(text, "\"minor_faults\":[null,null]") != NULL);
            CHECK(strstr(text, "\"available_samples\":2,\"median\":null") != NULL);
            CHECK(strstr(text, "\"available_samples\":0,\"median\":null") != NULL);
            CHECK(fclose(file) == 0);
        }
    }
}

static void test_legacy_schema(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "legacy-schema") && tp_mkdirs(directory) &&
                   test_text(directory, "complete.txt", "schema=1 jobs=1 pairs=20 rounds=2 guard=1\n") &&
                   tp_path(log, directory, "rejection.log");
    CHECK(paths_ok);
    if (paths_ok)
    {
        char* args[] = {(char*)executable, "child", "compare", directory, NULL};
        TpProcess result = tp_process(args, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2 && !result.launch_error && !result.timed_out);
    }
    FILE* file = paths_ok ? fopen(log, "rb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        char text[2048] = {0};
        (void)fread(text, 1, sizeof(text) - 1, file);
        CHECK(!ferror(file) && strstr(text, "unsupported result schema 1 (expected 2)") != NULL);
        CHECK(fclose(file) == 0);
    }
}

static void test_compile_options(void)
''')
replace(p, 'static int const expected[] = {0, 1, 0, 0, 0, 1, 2, 2, 2, 2};', 'static int const expected[] = {0, 1, 0, 0, 0, 1, 2, 2, 2, 2, 0, 0};')
replace(p, '        test_compile_options();\n', '''        test_process_fields(root);
        test_diagnostic_probes(root);
        test_legacy_schema(executable, root);
        test_compile_options();
''')
replace(p, '        test_processes(executable, root);\n', '''        test_processes(executable, root);
        printf("THROUGHPUT_RECORD_BYTES process=%zu row=%zu\\n", sizeof(TpProcess), sizeof(TpRow));
''')

p = 'tools/throughput/README.md'
texts[p] += '''

## Result schema 2: process diagnostics (#345)

The generated-input format remains **schema 1**: changing result columns does
not alter the six workload files, PRNG sequence, flags, modes or default guard
family. The result bundle is **schema 2**. Its CSV files append `minor_faults`,
`major_faults`, `voluntary_context_switches` and `involuntary_context_switches`
after `output_sha256`. These are optional exact unsigned integer counts; `NA`
means unavailable, whereas `0` is an observed zero. Negative, fractional,
malformed and overflowing counts are rejected. Raw values retain all 64 bits;
summary medians use the same floating-point presentation as other metrics.

Linux copies `ru_minflt`, `ru_majflt`, `ru_nvcsw` and `ru_nivcsw` from the
**existing per-invocation `wait4` result**, after stopping the wall clock. No
extra process, timer, PMU event, polling loop or allocation observer is enabled
in a timing trial. These are child usage counts, not the harness's cumulative
children totals. The operating system may include descendants whose resource
usage the child waited for; this is not live process-tree aggregation. macOS
and Windows currently leave these four observations unavailable. In particular,
a Windows total fault count is not invented as a minor/major split.

Both `samples.csv` and the independent PMU/allocation `telemetry.csv` retain
the counts. `summary.json` includes their timing medians and separate replay
medians with availability counts; an incomplete diagnostic series has a null
median rather than silently selecting its available subset. An unavailable OS
counter does not invalidate an otherwise complete allocation probe. Missing
allocation metrics still invalidate an explicitly requested allocation probe.
The existing output-hash, source-work and bundle-completion checks apply.

**These fields never gate performance.** The two rounds, adjacent pairs,
15%/2 ms wall margin, 20%/16 MiB RSS margin, exact sign test, Bonferroni family,
warmups, pair minimum and inconclusive status are unchanged. Synthetic tests
increase all four diagnostics by twelve orders of magnitude without changing
a guard decision. They are evidence for investigating scheduling or paging,
not grounds for deleting inconvenient samples or attributing a speedup.

The new reader rejects a schema-1 result with an explicit version diagnostic;
it does not reinterpret old column positions. Keep the original harness to
replay an old bundle, or collect a fresh experiment with schema 2. Do not edit
an old completion manifest to claim compatibility.

The native regression suite checks exact zero/UINT64_MAX/unavailable replay,
invalid counters, legacy rejection, optional probe completeness, and unchanged
corpus hashing. On Linux it compares one allocation-and-sleep child's `wait4`
counts with an independently obtained `getrusage(RUSAGE_CHILDREN)` delta,
without another child between snapshots. Other platforms check unavailability.
The existing Linux harness job additionally runs the suite under ASan/UBSan:

```sh
clang -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \\
  -fwrapv -fno-strict-aliasing -funsigned-char \\
  -fsanitize=address,undefined -fno-sanitize-recover=all \\
  tools/throughput/tests.c -lm -o build/throughput-tests-sanitized
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \\
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \\
  build/throughput-tests-sanitized build/throughput-tool-tests-sanitized
```

This closes a collection gap, not physical-hardware acceptance. Hosted runner
labels, generic PMU availability and these OS counters do not establish Zen 5
identity, physical isolation, instruction throughput or a compiler speedup.
#46/#128 still require verified physical Zen 5 experiments and separate Zen 4
nonregression. #346's optional macro/aggregate workloads remain separate; no
preprocessing/debug-specific case or default corpus expansion is added here.
'''

p = '.github/workflows/compiler-throughput.yml'
artifact_marker = '      - uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02\n        if: always()\n        with:\n          name: throughput-harness-${{ matrix.os }}\n'
replace(p, artifact_marker, '''      - name: Native harness sanitizers (Linux)
        if: ${{ cancelled() == false && runner.os == 'Linux' }}
        shell: bash
        env:
          ASAN_OPTIONS: halt_on_error=1:detect_leaks=1
          UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1
        run: |
          set -euo pipefail
          mkdir -p build
          clang -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fwrapv -fno-strict-aliasing -funsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all tools/throughput/tests.c -lm -o build/throughput-tests-sanitized
          build/throughput-tests-sanitized build/throughput-tool-tests-sanitized
''' + artifact_marker)
replace(p, '            build/throughput-tool-tests\n', '            build/throughput-tool-tests\n            build/throughput-tool-tests-sanitized\n')

for name, text in texts.items():
    Path(name).write_bytes(text.encode('utf-8'))
    print('Updated', name)
