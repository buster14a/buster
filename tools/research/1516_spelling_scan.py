#!/usr/bin/env python3
"""Bounded, branch-only #1516/#1525 functional experiment; not a timing harness.

Run only on an ephemeral GitHub-hosted Ubuntu runner. All build policy remains
in build.c. The existing #1525 change is applied only to a disposable checkout.
No production source, CI policy, benchmark service or admission data is updated.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

MAIN = "8f67df736f13d4edc055110a7a6d619a00a22eaf"
PR_HEAD = "6bc106b08f96ec56a5dfc54c8a0a6fbcd93491be"
PR_BASE = "33435221dec201c26cb9247959d7fcd00434f003"
FILES = ["src/buster/lib/compiler/driver/driver.c", "src/buster/lib/compiler/frontend/c/c.h", "src/buster/lib/compiler/frontend/c/c_source.c"]


def sha(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    work, candidate_checkout, out = [Path(p).resolve() for p in sys.argv[1:]]
    out.mkdir(parents=True, exist_ok=True)
    (out / "logs").mkdir(exist_ok=True)
    records: list[dict] = []

    def run(argv: list[str], label: str, *, cwd: Path = work, env: dict[str, str] | None = None, required: bool = True) -> subprocess.CompletedProcess:
        actual_env = dict(os.environ)
        for key in ("BUSTER_RESEARCH_FORCE_SCAN", "BUSTER_RESEARCH_CAPTURE", "BUSTER_RESEARCH_POST_SUM"):
            actual_env.pop(key, None)
        actual_env.update(env or {})
        print("EXEC", label, json.dumps(argv), flush=True)
        result = subprocess.run(argv, cwd=cwd, env=actual_env, capture_output=True, timeout=1200)
        (out / "logs" / f"{label}.stdout").write_bytes(result.stdout)
        (out / "logs" / f"{label}.stderr").write_bytes(result.stderr)
        records.append({"label": label, "argv": argv, "cwd": str(cwd), "environment_delta": env or {}, "returncode": result.returncode,
                        "stdout_sha256": hashlib.sha256(result.stdout).hexdigest(), "stderr_sha256": hashlib.sha256(result.stderr).hexdigest()})
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if required and result.returncode:
            print(result.stdout[-12000:].decode(errors="replace"), file=sys.stderr)
            print(result.stderr[-12000:].decode(errors="replace"), file=sys.stderr)
            raise RuntimeError(f"{label} failed with {result.returncode}")
        return result

    assert run(["git", "rev-parse", "HEAD"], "source-identity").stdout.decode().strip() == MAIN
    assert run(["git", "rev-parse", "HEAD"], "owner-identity", cwd=candidate_checkout).stdout.decode().strip() == PR_HEAD
    run(["clang", "--version"], "clang")
    run(["uname", "-a"], "host")
    run(["python3", "tools/new_audit.py", "--newest"], "newest-audit")
    driver = out / "hosted-build"
    run(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g", "build.c", "-o", str(driver)], "bootstrap")
    run([str(driver), "generate"], "configure")
    run([str(driver), "build", "--config", "Release", "-t", "ide"], "build-reference")
    # Includes the repository's stage-2 bench as part of the fixed-point gate.
    # Its hosted timings are not used as performance evidence.
    run([str(driver), "test_self_host", "--config", "Release"], "reference-fixed-point")
    bins = out / "bin"
    bins.mkdir(exist_ok=True)
    reference = bins / "reference-ide"
    shutil.copy2(work / "build/Release/ide", reference)
    frozen = out / "frozen"
    frozen.mkdir(exist_ok=True)
    shutil.copytree(work / "src", frozen / "src", dirs_exist_ok=True)
    shutil.copytree(work / "build/generated", frozen / "build/generated", dirs_exist_ok=True)
    manifest = [(str(p.relative_to(frozen)), sha(p)) for p in sorted(frozen.rglob("*")) if p.is_file()]
    (out / "frozen-inputs.json").write_text(json.dumps(manifest, indent=2) + "\n")

    patch = run(["git", "diff", PR_BASE, PR_HEAD, "--", *FILES], "owner-patch", cwd=candidate_checkout).stdout
    assert patch and all(p.encode() in patch for p in FILES)
    patch_path = out / "owner-only.patch"
    patch_path.write_bytes(patch)
    run(["git", "apply", "--check", str(patch_path)], "check-owner-patch")
    run(["git", "apply", str(patch_path)], "apply-owner-patch")

    # The probe is TU-local. Only the existing owner's candidate is prototyped.
    source = work / FILES[2]
    text = source.read_text()
    def replace_once(old: str, new: str) -> None:
        nonlocal text
        assert text.count(old) == 1, (old, text.count(old))
        text = text.replace(old, new)
    replace_once("    result.detail->preprocessed.tokens = output_count;\n    if (!options.omit_spelled_bytes)\n",
                 "    result.detail->preprocessed.tokens = output_count;\n"
                 "    u64 research_visits = 0;\n"
                 "    u64 research_oversized = 0;\n"
                 "    bool research_force_scan = string_equal(os_get_environment_variable(S8(\"BUSTER_RESEARCH_FORCE_SCAN\")), S8(\"1\"));\n"
                 "    if (!options.omit_spelled_bytes || research_force_scan)\n")
    replace_once("            spelled_bytes += c_token_length(spelling_base, stream[token_index]);\n",
                 "            research_visits += 1;\n"
                 "            research_oversized += stream[token_index].length == C_TOKEN_LENGTH_OVERSIZED;\n"
                 "            spelled_bytes += c_token_length(spelling_base, stream[token_index]);\n")
    replace_once("    c_preprocess_respell_c23(space, &map, &result);\n", "    c_preprocess_respell_c23(space, &map, &result);\n" + r'''    {
        String8 research_capture = os_get_environment_variable(S8("BUSTER_RESEARCH_CAPTURE"));
        if (research_capture.length)
        {
            bool research_post = string_equal(os_get_environment_variable(S8("BUSTER_RESEARCH_POST_SUM")), S8("1"));
            u64 research_after = 0;
            if (research_post)
            {
                for (u64 index = 0; index < output_count; index += 1)
                {
                    research_after += c_token_length(space->base, result.tokens[index]);
                }
            }
            u64 research_start = 0;
            for (u64 index = 0; index < options.source_path.length; index += 1)
            {
                if (options.source_path.pointer[index] == '/' || options.source_path.pointer[index] == '\\')
                {
                    research_start = index + 1;
                }
            }
            String8 research_name = { .pointer = options.source_path.pointer + research_start,
                                     .length = options.source_path.length - research_start };
            String8 research_path = string_format_z(arena, S8("{S8}/{S8}.tsv"), research_capture, research_name);
            String8 research_record = string_format(arena,
                S8("tokens={u64}\nvisits={u64}\noversized_visits={u64}\nbytes={u64}\nomit={u64}\nforced={u64}\npost_sum_enabled={u64}\npost_sum={u64}\n"),
                output_count, research_visits, research_oversized, result.detail->preprocessed.bytes,
                (u64)options.omit_spelled_bytes, (u64)research_force_scan, (u64)research_post, research_after);
            bool research_written = file_write(research_path, BUSTER_SLICE_TO_BYTE_SLICE(research_record));
            (void)research_written;
            BUSTER_VALIDATE(research_written);
        }
    }
''')
    source.write_text(text)
    run(["git", "diff", "--check"], "probe-whitespace")
    (out / "complete-probe.patch").write_bytes(run(["git", "diff", "--", *FILES], "complete-probe").stdout)
    run([str(driver), "build", "--config", "Release", "-t", "ide"], "build-probe")
    probe = bins / "probe-ide"
    shutil.copy2(work / "build/Release/ide", probe)
    identity = {"main": MAIN, "owner_base": PR_BASE, "owner_head": PR_HEAD,
                "owner_patch_sha256": sha(patch_path), "probe_patch_sha256": sha(out / "complete-probe.patch"),
                "reference_binary_sha256": sha(reference), "probe_binary_sha256": sha(probe),
                "frozen_manifest_sha256": sha(out / "frozen-inputs.json"), "evidence_class": "hosted functional/census only; not timing or performance admission"}
    (out / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")

    fixtures = frozen / "fixtures"
    fixtures.mkdir(exist_ok=True)
    texts = {
        "macro.c": "#define WIDE long long\nWIDE value = 12;\n",
        "c23.c": "bool b;\n",
        "paste.c": '#define CAT(a,b) a##b\n#define STR(x) #x\nint CAT(va,lue)=12;\nconst char *s=STR(a b);\n',
        "input.i": "int value=3;\n",
        "empty.c": "\n",
        "literal.c": 'const char text[] = "' + "x" * 80000 + '";\n',
        "failure.c": "int f(void) { return absent; }\n",
        "batch_a.c": "int helper(void) {return 7;}\n",
        "batch_b.c": "int helper(void); int main(void) {return helper()!=7;}\n",
        "many.c": "\n".join(f"int f_{i}(int x) {{return x+{i};}}" for i in range(1000)) + "\n",
    }
    for name, body in texts.items():
        (fixtures / name).write_text(body)
    results: list[dict] = []
    common_output = frozen / "artifact.o"
    metrics_path = frozen / "artifact.metrics"

    def measure_case(name: str, arguments: list[str], report: str, *, post: bool = False, expected_exit: int = 0) -> None:
        report_args = ([] if report == "none" else ["-v"] if report == "verbose" else [f"-fsource-metrics={metrics_path}"] if report == "metrics" else ["-v", f"-fsource-metrics={metrics_path}"])
        argv_tail = ["cc", *arguments, *report_args, "-o", str(common_output)]
        observations = []
        for arm, binary in (("reference", reference), ("forced", probe), ("demand", probe)):
            label = f"{name}-{report}-{arm}"
            capture = out / "census" / label
            capture.mkdir(parents=True, exist_ok=True)
            common_output.unlink(missing_ok=True)
            metrics_path.unlink(missing_ok=True)
            extra = {} if arm == "reference" else {"BUSTER_RESEARCH_CAPTURE": str(capture), "BUSTER_RESEARCH_FORCE_SCAN": "1" if arm == "forced" else "0", "BUSTER_RESEARCH_POST_SUM": "1" if post else "0"}
            process = run([str(binary), *argv_tail], label, cwd=frozen, env=extra, required=False)
            assert (process.returncode == 0) == (expected_exit == 0), (label, process.returncode)
            counters = {}
            for p in sorted(capture.glob("*.tsv")):
                counters[p.name] = {k: int(v) for k, v in (line.split("=", 1) for line in p.read_text().splitlines())}
            if arm != "reference":
                assert counters, (label, "missing census")
                for entry in counters.values():
                    expected_visits = entry["tokens"] if arm == "forced" or report != "none" else 0
                    assert entry["visits"] == expected_visits, (label, entry)
                    assert entry["omit"] == (report == "none"), (label, entry)
                if name.startswith("c23") and (arm == "forced" or report != "none"):
                    assert list(counters.values())[0]["bytes"] == 6
                    assert list(counters.values())[0]["post_sum"] == 7
                if name.startswith("macro") and (arm == "forced" or report != "none"):
                    assert list(counters.values())[0]["bytes"] == 17
                    assert list(counters.values())[0]["tokens"] == 6
                if name.startswith("literal") and (arm == "forced" or report != "none"):
                    assert list(counters.values())[0]["oversized_visits"] == 1
            metrics = metrics_path.read_bytes() if metrics_path.is_file() else None
            source_rows = [line for line in (process.stdout + process.stderr).splitlines() if b"their spelling bytes" in line]
            if report in ("verbose", "both"):
                assert len(source_rows) == 1, (label, "missing verbose spelling row")
            observation = {"arm": arm, "returncode": process.returncode, "artifact_sha256": sha(common_output), "counters": counters,
                           "metrics_sha256": hashlib.sha256(metrics).hexdigest() if metrics else None,
                           "spelling_report_rows": [line.decode(errors="replace") for line in source_rows]}
            if observations:
                first, first_process, first_metrics = observations[0]
                assert observation["returncode"] == first["returncode"], label
                assert observation["artifact_sha256"] == first["artifact_sha256"], label
                if report not in ("verbose", "both"):
                    assert process.stderr == first_process.stderr, (label, "stderr")
                    assert process.stdout == first_process.stdout, (label, "stdout")
                else:
                    assert source_rows == [line for line in (first_process.stdout + first_process.stderr).splitlines() if b"their spelling bytes" in line], (label, "source report")
                assert metrics == first_metrics, (label, "metrics file")
            observations.append((observation, process, metrics))
        results.append({"name": name, "report": report, "argv_tail": argv_tail, "observations": [x[0] for x in observations]})
        (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print("PASS", name, report, flush=True)

    for name in ("macro", "c23", "paste", "input", "empty", "literal", "failure"):
        suffix = ".i" if name == "input" else ".c"
        args = ["-g0", "-fsyntax-only" if name in ("macro", "failure") else "-c", str(fixtures / (name + suffix))]
        if name == "c23":
            args.insert(0, "-std=c23")
        for report in ("none", "verbose", "metrics", "both"):
            measure_case(name, args, report, post=name == "c23", expected_exit=1 if name == "failure" else 0)
    unity = ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-c", "src/buster/apps/ide/ide.c"]
    for debug in ("-g0", "-g"):
        for report in ("none", "metrics"):
            measure_case("unity" + debug, [debug, *unity], report)
    for jobs in (1, 2):
        for report in ("none", "metrics"):
            measure_case(f"batch-{jobs}", ["-g0", f"-fcompile-jobs={jobs}", str(fixtures / "batch_a.c"), str(fixtures / "batch_b.c")], report)
    # Execute actual no-consumer, rare-consumer and always-consumed workloads.
    for profile in ("never", "rare", "always"):
        for iteration in range(8):
            report = "metrics" if profile == "always" or profile == "rare" and iteration == 7 else "none"
            measure_case(f"frequency-{profile}-{iteration}", ["-g0", "-c", str(fixtures / "many.c")], report)
    run([str(driver), "test_self_host", "--config", "Release"], "probe-fixed-point")
    summary = {"cases": len(results), "compiler_invocations": sum(len(r["observations"]) for r in results),
               "artifact_or_diagnostic_mismatches": 0, "c23_pre_bytes": 6, "c23_post_bytes": 7,
               "timing_measured": False, "benchpress_used": False,
               "scope": "Actual whole-command hosted functional comparisons and executed census, not full matrix, sanitizer or performance admission."}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
