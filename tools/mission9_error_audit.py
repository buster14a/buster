#!/usr/bin/env python3
"""Temporary Mission 9 correctness probe; no compiler build orchestration.

Run only on an authorized correctness runner. `instrument` changes an idle
checkout for a separate diagnostic build, never an acceptance/timing build.
Revision 2 corrects a probe expectation: the CLI prints the first error;
complete multi-error reporting belongs to CompilerDriverResult.diagnostics.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

BASE = "bb96b5d08e570aff3cbd473441d71ab0dc691d67"
FIXTURES = {
    "good_a.c": '#warning M9_EARLY_WARNING\nint helper_b(void);\nint helper_c(void);\nint main(void) { return helper_b() + helper_c() != 42; }\n',
    "good_b.c": 'int helper_b(void) { return 17; }\n',
    "good_c.c": '#warning M9_LATE_WARNING\nint helper_c(void) { return 25; }\n',
    "bad_parse.c": 'int broken(void) { return (1 + ); }\n',
    "bad_nested.c": 'struct Pair { int x; int y; };\nstruct Wrap { struct Pair pair; int tail; };\nint completed_declaration(void) { return 17; }\nint broken(void) {\n    struct Wrap w = { .pair = { .missing = 3 }, .tail = 7 };\n    return w.tail;\n}\n',
    "bad_multi.c": '#error M9_FIRST_ERROR\n#error M9_SECOND_ERROR\nint helper_b(void) { return 17; }\n',
}
FIXTURES["good_nested.c"] = FIXTURES["bad_nested.c"].replace(".missing", ".x")
CASES = [
    ("syntax_first_object", ["bad_parse.c"], False, True),
    ("nested_object", ["bad_nested.c"], False, True),
    ("after_work_syntax", ["good_a.c", "bad_parse.c", "good_c.c"], False, False),
    ("after_work_nested", ["good_a.c", "bad_nested.c", "good_c.c"], False, False),
    ("multi_errors", ["good_a.c", "bad_multi.c", "good_c.c"], False, False),
    ("failure_then_success", ["bad_nested.c", "good_c.c"], False, False),
    ("valid_object", ["good_b.c"], True, True),
    ("valid_nested_neighbor", ["good_nested.c"], True, True),
    ("valid_link", ["good_a.c", "good_b.c", "good_c.c"], True, False),
]
SENTINEL = b"M9 pre-existing user output\nnot a compiler artifact\x00\xff"


def replace_once(path, before, after):
    text = path.read_text()
    count = text.count(before)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one anchor, found {count}: {before[:80]}")
    path.write_text(text.replace(before, after))


def instrument(evidence):
    driver = Path("src/buster/lib/compiler/driver/driver.c")
    ide = Path("src/buster/apps/ide/ide.c")
    shutil.copy2(driver, evidence / "driver.original.c")
    shutil.copy2(ide, evidence / "ide.original.c")
    anchors = [
        ("    result.parser_diagnostic_count = syntax.diagnostic_count;", '    string_print(S8("M9_PARSE\\t{S8}\\t{u32}\\n"), invocation.input_paths[0], syntax.diagnostic_count);\n'),
        ("    result.analysis_diagnostic_count = lowered.diagnostic_count;", '    string_print(S8("M9_LOWER\\t{S8}\\t{u32}\\t{u32}\\n"), invocation.input_paths[0], (u32)(lowered.program != 0), lowered.diagnostic_count);\n'),
        ("    IrModule* module = &lowered.program->modules[0];", '    string_print(S8("M9_PREPARE\\t{S8}\\n"), invocation.input_paths[0]);\n'),
        ("    result.fast = module->fast;", '    string_print(S8("M9_VALIDATION\\t{S8}\\t{u32}\\n"), invocation.input_paths[0], (u32)validation.error);\n'),
        ("    CodegenModule code = codegen_generate_canonical_module_with_trace", '    string_print(S8("M9_BACKEND\\t{S8}\\n"), invocation.input_paths[0]);\n'),
        ("    ObjectFile object = object_from_canonical_codegen_module", '    string_print(S8("M9_OBJECT\\t{S8}\\n"), invocation.input_paths[0]);\n'),
        ("    result->native_link = link_native_executable", '    string_print(S8("M9_LINK\\n"));\n'),
        ("    result.native_link = link_native_executable", '    string_print(S8("M9_LINK\\n"));\n'),
    ]
    for before, prefix in anchors:
        replace_once(driver, before, prefix + before)
    before = "    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);"
    after = before + r'''
    if (os_get_environment_variable(S8("M9_REPORT")).length)
    {
        for (u32 m9_phase = 0; m9_phase < 3; m9_phase += 1)
        {
            CompilerDriverResult m9_observed;
            if (m9_phase == 1)
            {
                string_print(S8("M9_REPLAY_BEGIN\n"));
                String8 m9_root = os_get_environment_variable(S8("M9_REPEAT_ROOT"));
                String8 m9_inputs[] = {
                    string_format_z(arena, S8("{S8}/good_a.c"), m9_root),
                    string_format_z(arena, S8("{S8}/good_b.c"), m9_root),
                    string_format_z(arena, S8("{S8}/good_c.c"), m9_root),
                };
                CompilerDriverInvocation m9_again = invocation;
                m9_again.input_paths = m9_inputs;
                m9_again.input_count = BUSTER_ARRAY_LENGTH(m9_inputs);
                m9_again.input_languages = 0;
                m9_again.input_language_count = 0;
                m9_again.action = COMPILER_DRIVER_ACTION_LINK;
                m9_again.output_path = os_get_environment_variable(S8("M9_REPEAT_OUTPUT"));
                m9_observed = compiler_driver_execute_invocation(arena, m9_again);
            }
            else
            {
                m9_observed = compile;
            }
            string_print(S8("M9_RESULT\t{u32}\t{u32}\t{u32}\t{u32}\t{u32}\t{u32}\t{u32}\n"),
                m9_phase, (u32)m9_observed.error, m9_observed.diagnostic_count,
                m9_observed.parser_diagnostic_count, m9_observed.analysis_diagnostic_count,
                (u32)m9_observed.has_object, m9_observed.compilation_workers);
            for (u32 m9_index = 0; m9_index < m9_observed.diagnostic_count; m9_index += 1)
            {
                CompilerDiagnostic m9_record = m9_observed.diagnostics[m9_index];
                string_print(S8("M9_DIAG\t{u32}\t{u32}\t{S8}\t{u32}\t{S8}\t{u32}\t{u32}\t{u32}\t{S8}\t{u32}\t{u32}\t{S8}\n"),
                    m9_phase, m9_index, m9_record.code, (u32)m9_record.severity,
                    m9_record.primary.path, m9_record.primary.position.line,
                    m9_record.primary.position.column, (u32)m9_record.primary.has_range,
                    m9_record.primary.original_path, m9_record.primary.original_position.line,
                    m9_record.primary.original_position.column, m9_record.message);
            }
        }
    }
'''
    replace_once(ide, before, after)
    diff = subprocess.run(["git", "diff", "--", str(driver), str(ide)], check=True, text=True, capture_output=True)
    (evidence / "instrumentation.patch").write_text(diff.stdout)
    shutil.copy2(driver, evidence / "driver.instrumented.c")
    shutil.copy2(ide, evidence / "ide.instrumented.c")


def execute(command, destination, env=None, timeout=90):
    record = {"command": command, "cwd": str(Path.cwd()), "timeout_seconds": timeout}
    try:
        result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=timeout)
        record.update(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)
    except subprocess.TimeoutExpired as error:
        record.update(returncode=None, timeout=True,
                      stdout=(error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or ""),
                      stderr=(error.stderr or b"").decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or ""))
    except OSError as error:
        record.update(returncode=None, launch_error=str(error), stdout="", stderr="")
    destination.write_text(json.dumps(record, indent=2))
    return record


def run_probe(binary, evidence, diagnostic, screen):
    root = Path(".mission9-cases")
    root.mkdir(exist_ok=True)
    for name, source in FIXTURES.items():
        (root / name).write_text(source)
    shutil.copytree(root, evidence / "fixtures", dirs_exist_ok=True)
    manifest = {name: hashlib.sha256(source.encode()).hexdigest() for name, source in FIXTURES.items()}
    (evidence / "fixture-sha256.json").write_text(json.dumps(manifest, indent=2))
    binary = str(Path(binary).resolve())
    outcomes = []
    failures = []
    matrix = [("fast", "-ffrontend-ssa", 1, "absent")] if screen else [
        (allocator, frontend, jobs, state)
        for allocator in ("none", "mir-stack", "fast", "quality")
        for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa")
        for jobs in (1, 2)
        for state in ("absent", "sentinel")
    ]
    for allocator, frontend, jobs, state in matrix:
        for name, inputs, valid, object_only in CASES:
            key = f"{allocator}-{frontend[2:]}-jobs{jobs}-{state}-{name}"
            case_dir = evidence / key
            case_dir.mkdir(parents=True, exist_ok=False)
            output = case_dir / ("output.o" if object_only else "output")
            replay_output = case_dir / "replay-output"
            if state == "sentinel":
                output.write_bytes(SENTINEL)
            command = [binary, "cc", "-std=c11", "-target", "x86_64-unknown-linux", "-fverify-codegen",
                       f"-fregister-allocator={allocator}", frontend, f"-fcompile-jobs={jobs}"]
            if object_only:
                command.append("-c")
            command += [str(root / source) for source in inputs] + ["-o", str(output)]
            env = os.environ.copy()
            if diagnostic:
                env.update(M9_REPORT="1", M9_REPEAT_ROOT=str(root), M9_REPEAT_OUTPUT=str(replay_output))
            result = execute(command, case_dir / "compile.json", env)
            errors = []
            status = result["returncode"]
            text = result["stdout"] + result["stderr"]
            if valid:
                if status != 0 or not output.is_file() or not output.stat().st_size:
                    errors.append("valid control failed or did not publish nonempty output")
                elif not object_only:
                    run = execute([str(output.resolve())], case_dir / "program.json", timeout=15)
                    if run["returncode"] != 0:
                        errors.append("valid executable did not return 0")
            else:
                if status is None or status <= 0 or status >= 128:
                    errors.append("invalid input did not terminate with ordinary unsuccessful status")
                if "cc: error:" not in text:
                    errors.append("missing CLI diagnostic")
                if state == "sentinel" and (not output.exists() or output.read_bytes() != SENTINEL):
                    errors.append("failed transaction changed pre-existing output")
                if state == "absent" and output.exists():
                    errors.append("failed transaction published new output")
                if not diagnostic and "M9_LATE_WARNING" in text:
                    errors.append("diagnostic from unobservable later unit was published")
                if inputs[0] == "good_a.c" and "M9_EARLY_WARNING" not in text:
                    errors.append("earlier useful warning was lost")
                # docs/diagnostics.md and run_c_compiler select first-error text.
                if name == "multi_errors" and "M9_FIRST_ERROR" not in text:
                    errors.append("first-error compatibility text was lost")
            extra = sorted(path.name for path in case_dir.iterdir()
                           if path.name not in ("output", "output.o", "compile.json", "program.json", "replay-output", "replay-program.json"))
            if extra:
                errors.append(f"unexpected artifact names: {extra}")
            detail = {}
            if diagnostic:
                original_trace = result["stdout"].split("M9_REPLAY_BEGIN", 1)[0]
                records = {phase: [] for phase in range(3)}
                snapshots = {}
                for line in result["stdout"].splitlines():
                    if line.startswith("M9_RESULT\t"):
                        fields = line.split("\t")
                        snapshots[int(fields[1])] = [int(value) for value in fields[2:]]
                    if line.startswith("M9_DIAG\t"):
                        fields = line.split("\t")
                        records[int(fields[1])].append(fields[2:])
                for index, line in enumerate(original_trace.splitlines()):
                    if line.startswith("M9_BACKEND\t"):
                        source = line.split("\t")[1]
                        previous = original_trace.splitlines()[:index]
                        if f"M9_VALIDATION\t{source}\t0" not in previous:
                            errors.append("backend was not preceded by successful canonical validation")
                if not valid:
                    if "M9_LATE_WARNING" in original_trace + result["stderr"]:
                        errors.append("diagnostic from unobservable later unit was published")
                    if "M9_LINK" in original_trace:
                        errors.append("failed transaction reached final native linker")
                    for source in inputs:
                        if source.startswith("bad_") and f"M9_BACKEND\t{root / source}" in original_trace:
                            errors.append("failed TU reached backend")
                    if not records[0] or not any(row[2] == "0" for row in records[0]):
                        errors.append("no structured error record")
                    for row in records[0]:
                        if row[2] == "0" and (not row[1].startswith("c.") or int(row[4]) == 0 or int(row[5]) == 0 or not row[3]):
                            errors.append("structured source diagnostic lacks stable code/path/position")
                    if snapshots.get(0, [0])[0] == 0:
                        errors.append("driver result lost its failure")
                    if name.startswith("after_work_") and f"M9_BACKEND\t{root / 'good_a.c'}" not in original_trace:
                        errors.append("earlier TU did not demonstrate backend work")
                    if name == "multi_errors":
                        messages = [row[-1] for row in records[0] if row[2] == "0"]
                        if not all(any(marker in message for message in messages)
                                   for marker in ("M9_FIRST_ERROR", "M9_SECOND_ERROR")):
                            errors.append("complete structured multi-error reporting was lost")
                if records[0] != records[2] or snapshots.get(0) != snapshots.get(2):
                    errors.append("retained first result changed after second invocation")
                if snapshots.get(1, [-1])[0] != 0 or not replay_output.is_file():
                    errors.append("second in-process valid invocation failed")
                else:
                    replay = execute([str(replay_output.resolve())], case_dir / "replay-program.json", timeout=15)
                    if replay["returncode"] != 0:
                        errors.append("second invocation executable failed")
                detail = {"snapshots": snapshots, "diagnostics": records,
                          "original_trace": original_trace,
                          "later_success_completed": f"M9_BACKEND\t{root / 'good_c.c'}" in original_trace}
            row = {"case": key, "returncode": status, "valid": valid, "errors": errors, **detail}
            outcomes.append(row)
            if errors:
                failures.append(row)
            print("M9_CASE " + json.dumps(row), flush=True)
    summary = {"base": BASE, "probe_revision": 2, "binary": binary,
               "binary_sha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
               "diagnostic": diagnostic, "screen": screen, "cases": len(outcomes),
               "failures": len(failures), "outcomes": outcomes}
    (evidence / "summary.json").write_text(json.dumps(summary, indent=2))
    print(f"M9_SUMMARY cases={len(outcomes)} failures={len(failures)} diagnostic={diagnostic}", flush=True)
    return bool(failures)


def references(evidence):
    root = Path(".mission9-cases")
    root.mkdir(exist_ok=True)
    failed = False
    for name, text in FIXTURES.items():
        (root / name).write_text(text)
        row = execute(["clang", "-std=c11", "-fsyntax-only", str(root / name)], evidence / (name + ".json"))
        expected_valid = name.startswith("good_")
        okay = row["returncode"] == 0 if expected_valid else row["returncode"] is not None and 0 < row["returncode"] < 128
        failed |= not okay
        print(f"M9_REFERENCE source={name} returncode={row['returncode']} expected_valid={expected_valid} okay={okay}")
    return failed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("instrument", "run", "references"))
    parser.add_argument("--binary", default="build/Release/ide")
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--screen", action="store_true")
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    result = False
    if args.mode == "instrument":
        instrument(args.evidence)
    elif args.mode == "references":
        result = references(args.evidence)
    else:
        result = run_probe(args.binary, args.evidence, args.diagnostic, args.screen)
    return int(result)


if __name__ == "__main__":
    sys.exit(main())
