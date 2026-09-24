#!/usr/bin/env python3
"""Hosted-only reproduction. Counts and correctness, never performance evidence.

The first script is preserved byte-for-byte to explain the failed first attempt.
This revision corrects its three source anchors and printf width, then tests the
candidate against the unchanged compiler. All source edits are disposable here.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
script = (HERE / "probe.py").read_text()
script = script.replace('if (!requires_expansion)', 'if (needs_expansion)')
script = script.replace('PP_REUSE_ARG %u %u', 'PP_REUSE_ARG %llu %u')
script = script.replace('argument->token_count, (unsigned)requires_expansion', '(unsigned long long)argument->token_count, (unsigned)needs_expansion')
script = script.replace('TemporalArena paste_scratch =', 'TemporalArena paste_temporary =')
script = script.replace('(int)spelling.length, (char const*)spelling.pointer', '(int)joined_length, (char const*)joined')
script = script.replace('diagnostic-provenance.stderr', 'diagnostic-provenance.stdout')
# Source census only: independently classify demand on the existing definition.
# Count both construction and avoided scans. This work is NOT a timing subject.
extra = r'''
        bool probe_demand = continuation->macro->definition.pragma_like;
        CMacroDefinition* probe_definition = &continuation->macro->definition;
        for (u32 probe_index = 0; probe_index < probe_definition->replacement_count && !probe_demand; probe_index += 1)
        {
            if (probe_definition->parameter_index[probe_index] == continuation->argument_index)
            {
                bool probe_raw = (probe_index && (c_token_is_punctuator(&probe_definition->replacement[probe_index - 1], C_PUNCTUATOR_HASH) ||
                                                  c_macro_is_paste(probe_definition->replacement[probe_index - 1]))) ||
                                 (probe_index + 1 < probe_definition->replacement_count && c_macro_is_paste(probe_definition->replacement[probe_index + 1]));
                probe_demand = !probe_raw;
            }
        }
        fprintf(stderr, "PP_REUSE_DEMAND %u %llu %u\n", (unsigned)probe_demand, (unsigned long long)argument->token_count, (unsigned)needs_expansion);
'''
needle = 'source_path.write_text(instrumented)'
replacement = 'instrumented = instrumented[:point] + extra + instrumented[point:]\n' + needle
# Recompute the argument insertion point; the old point now names the paste site.
replacement = 'point = instrumented.index("        if (needs_expansion)", instrumented.index(" c_macro_continuation_advance("))\n' + replacement
assert script.count(needle) == 1
script = script.replace(needle, replacement)
exec(compile(script, str(HERE / "probe.py"), "exec"), globals())

(OUT / "executed_probe.py").write_text(script)
(OUT / "base-object-hashes.json").write_text(json.dumps(base_hashes, indent=2) + "\n")
demand = {}
for name in workloads:
    rows = [line.split()[1:] for line in (OUT / (name + "-probe.stderr")).read_text().splitlines() if line.startswith("PP_REUSE_DEMAND ")]
    demand[name] = {
        "arguments": len(rows),
        "no_expanded_use": sum(row[0] == "0" for row in rows),
        "no_expanded_use_tokens": sum(int(row[1]) for row in rows if row[0] == "0"),
        "unneeded_child_expansions": sum(row[0] == "0" and row[2] == "1" for row in rows),
        "zero_token_arguments": sum(row[1] == "0" for row in rows),
        "note": "Diagnostic counts include nested invocations; eliminated nested work can remove additional argument preparations."
    }
(OUT / "argument-demand.json").write_text(json.dumps(demand, indent=2) + "\n")
print("PP_REUSE_DEMAND_SUMMARY " + json.dumps(demand), flush=True)

# Restore the exact uninstrumented source before applying the two-file patch.
source_path.write_text(original)
run(["git", "apply", "--recount", "--check", HERE / "change.patch"], "patch-check")
run(["git", "apply", "--recount", HERE / "change.patch"], "patch-apply")
run(["git", "diff", "--check"], "diff-check")
test_path = ROOT / "src/buster/tests/compiler/frontend/c/macro_conditional_test.c"
candidate_source = source_path.read_text()
(OUT / "candidate.patch").write_bytes(subprocess.check_output(["git", "diff", "--", str(source_path), str(test_path)]))
run([driver, "build", "--build-directory", "build-pp-reuse", "--config", "Release", "-t", "ide"], "build-candidate", timeout=1500)
candidate = OUT / "ide-candidate"
shutil.copy2(ide, candidate)

candidate_rows = []
for name, (source, expected) in cases.items():
    for dialect in ("gnu17", "c17"):
        result = run([candidate, "cc", "-E", "-nostdinc", "-std=" + dialect, inputs / (name + ".c")], name + "-" + dialect + "-candidate", required=False)
        candidate_rows.append({"case": name, "dialect": dialect, "exit": result.returncode,
                               "matches_expected": result.returncode == 0 and tokens(result.stdout) == tokens(expected.encode())})
(OUT / "candidate-counterexamples.json").write_text(json.dumps(candidate_rows, indent=2) + "\n")
print("PP_REUSE_CANDIDATE " + json.dumps(candidate_rows), flush=True)
assert all(row["matches_expected"] for row in candidate_rows)

# Additional discrimination: reference-compiler agreement is an independent
# oracle, not an expectation copied from Buster. Includes mixed use and GNU
# comma deletion with a nested same-name macro.
extra_cases = {}
pattern = r'\{S8\(("(?:\\.|[^"\\])*")\), S8\(("(?:\\.|[^"\\])*")\), [0-9]+, [0-9]+\}'
for index, match in enumerate(re.finditer(pattern, test_path.read_text())):
    extra_cases["demand-" + str(index)] = json.loads(match.group(1))
extra_cases["gnu-comma-nested"] = "#define G(x,...) x , ## __VA_ARGS__\nG(0,G(1))\n"
extra_cases["suffix-ordinary"] = "#define F(x) x\n#define ALIAS F\n#define ID(x) x\nID(ALIAS)(7)\n"
extra_rows = []
for name, source in extra_cases.items():
    path = inputs / (name + ".c")
    path.write_text(source)
    for dialect in ("gnu17", "c17"):
        outputs = {}
        for label, command in (("candidate", [candidate, "cc", "-E"]), ("clang", ["clang", "-E", "-P"]), ("gcc", ["gcc", "-E", "-P"])):
            result = run(command + ["-nostdinc", "-std=" + dialect, path], name + "-" + dialect + "-" + label, required=False)
            outputs[label] = {"exit": result.returncode, "tokens": tokens(result.stdout) if result.returncode == 0 else []}
        extra_rows.append({"case": name, "dialect": dialect, "outputs": outputs,
                           "agrees": outputs["candidate"] == outputs["clang"] == outputs["gcc"]})
(OUT / "extra-compatibility.json").write_text(json.dumps(extra_rows, indent=2) + "\n")
print("PP_REUSE_EXTRA " + json.dumps(extra_rows), flush=True)
assert all(row["agrees"] for row in extra_rows)

object_rows = {}
for name, flags in workloads.items():
    run([candidate, "cc"] + flags + ["-o", artifact], name + "-candidate", timeout=300)
    object_rows[name] = {"base_sha256": base_hashes[name], "candidate_sha256": digest(artifact), "identical": base_hashes[name] == digest(artifact)}
(OUT / "candidate-objects.json").write_text(json.dumps(object_rows, indent=2) + "\n")
print("PP_REUSE_OBJECTS " + json.dumps(object_rows), flush=True)
assert all(row["identical"] for row in object_rows.values())

# Registered module, selected only in this diagnostic executable. Ordinary CI
# and the published test runner are unchanged. Preserve the selection patch.
runner_path = ROOT / "src/buster/tests/test.c"
runner_original = runner_path.read_text()
runner_selected = runner_original.replace(
    'result = buster_test_run_parallel_descriptors(arguments, test_descriptors, BUSTER_ARRAY_LENGTH(test_descriptors), timing_enabled, &timing_record_count);',
    'result = buster_test_run_descriptors(arguments, test_descriptors + TEST_ID_C_MACRO_CONDITIONAL, 1, timing_enabled, &timing_record_count, TEST_ID_C_MACRO_CONDITIONAL);')
runner_selected = runner_selected.replace(
    'for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(test_descriptors); index += 1)',
    'for (u64 index = TEST_ID_C_MACRO_CONDITIONAL; index < TEST_ID_C_MACRO_CONDITIONAL + 1; index += 1)')
assert runner_selected != runner_original
runner_path.write_text(runner_selected)
(OUT / "diagnostic-module-selection.patch").write_bytes(subprocess.check_output(["git", "diff", "--", str(runner_path)]))
source_path.write_text(original)
run([driver, "generate", "--build-directory", "build-pp-reuse", "--config", "Release", "--cc", "clang", "--ci", "--linker", "DEFAULT", "-DBUSTER_INCLUDE_TESTS=ON"], "generate-tests", timeout=300)
run([driver, "build", "--build-directory", "build-pp-reuse", "--config", "Release", "-t", "ide"], "build-tests-base", timeout=1500)
baseline_tests = run([ide, "test", "--verbose=1", "--ci=1"], "module-base", required=False, timeout=300)
assert baseline_tests.returncode != 0, "new regression must fail on unchanged source"
source_path.write_text(candidate_source)
run([driver, "build", "--build-directory", "build-pp-reuse", "--config", "Release", "-t", "ide"], "build-tests-candidate", timeout=1500)
run([ide, "test", "--verbose=1", "--ci=1"], "module-candidate", timeout=300)
print("PP_REUSE_REGISTERED_MODULE\n" + (OUT / "module-candidate.stdout").read_text(), flush=True)
runner_path.write_text(runner_original)

# Two-generation correctness fixed points. No bench subcommand, timing, PMU or
# RSS collection. Baseline uses its frozen original source; candidate uses its
# own two-file source tree. Preserve all argv and executable hashes.
fixed_points = {}
for label, compiler, source_root, generated in (("base", base, frozen, frozen / "generated"),
                                               ("candidate", candidate, ROOT, ROOT / "build-pp-reuse/generated")):
    stage1 = OUT / (label + "-self-1")
    stage2 = OUT / (label + "-self-2")
    flags = ["-g", "-I" + str(source_root / "src"), "-I" + str(generated), "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", source_root / "src/buster/apps/ide/ide.c", "-lm"]
    run([compiler, "cc"] + flags + ["-o", stage1], label + "-self-1", timeout=300)
    run([stage1, "cc"] + flags + ["-o", stage2], label + "-self-2", timeout=300)
    fixed_points[label] = {"stage1": digest(stage1), "stage2": digest(stage2), "identical": digest(stage1) == digest(stage2)}
(OUT / "fixed-points.json").write_text(json.dumps(fixed_points, indent=2) + "\n")
print("PP_REUSE_FIXED_POINTS " + json.dumps(fixed_points), flush=True)
assert all(row["identical"] for row in fixed_points.values())
(OUT / "candidate-identities.json").write_text(json.dumps({"source_blob": subprocess.check_output(["git", "hash-object", str(source_path)], text=True).strip(), "test_blob": subprocess.check_output(["git", "hash-object", str(test_path)], text=True).strip(), "binary_sha256": digest(candidate)}, indent=2) + "\n")
run(["git", "diff", "--check"], "final-diff-check")
print("PP_REUSE_COMPLETE performance=UNMEASURED selected_module=passed full_CI=not_run sanitizer=not_run", flush=True)
