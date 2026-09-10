"""Apply the reviewed Z16 harness-only changes to a frozen source checkout."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'tools/throughput/throughput.c'
s = p.read_text()
def replace(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:100])
    s = s.replace(old, new)
replace('    int cpu, pmu, require_pmu, guard, identical;', '    int cpu, pmu, require_pmu, guard, identical;\n    int emit_assembly;')
replace('            else if (!strcmp(key, "--flag"))\n', '''            else if (!strcmp(key, "--emit"))
            {
                ok = !strcmp(value, "object") || !strcmp(value, "assembly");
                if (ok) config->emit_assembly = !strcmp(value, "assembly");
            }
            else if (!strcmp(key, "--flag"))
''')
replace('        args[argc++] = "-c";', '        args[argc++] = config->emit_assembly ? "-S" : "-c";')
replace('    snprintf(leaf, sizeof(leaf), "%s-%s-%u%s", job->workload.name, tp_modes[job->mode], variant, job->stage ? ".exe" : ".o");', '''    char const* extension = job->stage ? ".exe" : config && config->emit_assembly ? ".s" : ".o";
    snprintf(leaf, sizeof(leaf), "%s-%s-%u%s", job->workload.name, tp_modes[job->mode], variant, extension);''')
replace('        tp_json_string(file, config->profile);\n', r'''        tp_json_string(file, config->profile);
        fputs(",\"synthetic_output\":", file);
        tp_json_string(file, config->emit_assembly ? "assembly" : "object");
''')
replace('          "--timeout SECONDS, --cpu N|auto, --flag ARG (repeatable), --baseline-id LABEL, --candidate-id LABEL,\\n"', '          "--emit object|assembly (synthetic inputs only; default object),\\n"\n          "--timeout SECONDS, --cpu N|auto, --flag ARG (repeatable), --baseline-id LABEL, --candidate-id LABEL,\\n"')
p.write_text(s)
p = root / 'tools/throughput/tests.c'
s = p.read_text()
start = s.index('static void test_compile_options(void)')
end = s.index('\nstatic void test_sample_paths', start)
s = s[:start] + '''static void test_compile_options(void)
{
    char* options[] = {"throughput", "generate", "--flag", "-O3", "--flag", "-O0", NULL};
    TpConfig config;
    CHECK(tp_options(6, options, &config) && !config.emit_assembly);
    config.self_host_generated = "generated";
    for (unsigned emission = 0; emission < 2; ++emission)
    {
        config.emit_assembly = (int)emission;
        for (unsigned stage = 0; stage < 3; ++stage)
        {
            for (unsigned mode = 0; mode < 4; ++mode)
            {
                TpJob job = {0};
                job.mode = mode;
                job.stage = stage;
                strcpy(job.workload.path, "input.c");
                TpArguments args;
                tp_compile_arguments(&config, &job, "ide", "output", "metrics", &args);
                unsigned selected = 0, last_optimization = 0, selections = 0, objects = 0, assemblies = 0;
                for (unsigned i = 0; i < args.count; ++i)
                {
                    if (!strncmp(args.values[i], "-O", 2)) last_optimization = i;
                    if (!strncmp(args.values[i], "-fregister-allocator=", 21)) { selected = i; ++selections; }
                    if (!strcmp(args.values[i], "-c")) ++objects;
                    if (!strcmp(args.values[i], "-S")) ++assemblies;
                }
                CHECK(selections == 1 && selected > last_optimization &&
                      !strcmp(args.values[selected] + 21, tp_modes[mode]) && args.values[args.count] == NULL);
                CHECK(objects == (unsigned)(!stage && !emission) && assemblies == (unsigned)(!stage && emission));
            }
        }
    }
    char* assembly[] = {"throughput", "generate", "--emit", "assembly", NULL};
    CHECK(tp_options(4, assembly, &config) && config.emit_assembly);
    char* object[] = {"throughput", "generate", "--emit", "assembly", "--emit", "object", NULL};
    CHECK(tp_options(6, object, &config) && !config.emit_assembly);
    char* invalid[] = {"throughput", "generate", "--emit", "executable", NULL};
    CHECK(!tp_options(4, invalid, &config));
    char* missing[] = {"throughput", "generate", "--emit", NULL};
    CHECK(!tp_options(3, missing, &config));
    char* forbidden_values[] = {"-fno-register-allocator", "-S", "-c", "-E", "-ooutput", "-fsource-metrics=other"};
    for (unsigned i = 0; i < sizeof(forbidden_values) / sizeof(forbidden_values[0]); ++i)
    {
        char* forbidden[] = {"throughput", "generate", "--flag", forbidden_values[i], NULL};
        CHECK(!tp_options(4, forbidden, &config));
    }
}
''' + s[end:]
old = '    CHECK(tp_generate(&config, a, first) && tp_generate(&config, b, second));'
assert s.count(old) == 1
s = s.replace(old, '''    CHECK(tp_generate(&config, a, first));
    config.emit_assembly = 1;
    CHECK(tp_generate(&config, b, second));''')
p.write_text(s)
p = root / 'tools/throughput/README.md'
s = p.read_text()
needle = 'Tiny startup is **warm-filesystem, fresh-process latency**, not a cold disk\n'
assert s.count(needle) == 1
s = s.replace(needle, '''`--emit assembly` selects `cc -S` and `.s` artifacts for the synthetic
workloads; `--emit object` is the unchanged default. This uses the same
launcher, deterministic corpus, paired trials, independent diagnostic replays,
output-identity checks and statistical guard, not a second benchmark runner.
The option does not change frozen-source self-host stages: those still emit
executables and measure separate compiler subjects. Metadata records
`synthetic_output`, and the command log retains the exact operation. `--flag`
continues to reject `-S`, `-c`, `-E`, output paths and allocator overrides.

For a representation-only printer change, run both output kinds in separate,
fresh bundles with identical profiles, seeds, scales, modes and common flags:

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-assembly --emit assembly --require-identical-output --cpu auto
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-object --emit object --require-identical-output --cpu auto
```

Assembly timings include index construction, formatting and final artifact
publication; object timings are the unchanged-path control. Do not compare an
assembly-output sample with an object-output sample as an A/B optimization
result. Geometric `--scale` values retain the existing corpus definitions;
`many_functions` has 512 definitions per `ci` scale unit. Physical Zen 5
acceptance and an independent assembler remain separate requirements.

''' + needle)
p.write_text(s)
