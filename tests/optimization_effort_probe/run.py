#!/usr/bin/env python3
"""Frozen generated-code identity/execution probe, deliberately without timing."""
import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(argv, prefix, timeout=240):
    prefix.parent.mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run([str(arg) for arg in argv], cwd=REPO, capture_output=True, timeout=timeout)
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as exc:
        stdout, stderr, code = exc.stdout or b"", exc.stderr or b"", 124
    prefix.with_suffix(".stdout").write_bytes(stdout)
    prefix.with_suffix(".stderr").write_bytes(stderr)
    return {"argv": [str(arg) for arg in argv], "returncode": code,
            "stdout": str(prefix.with_suffix(".stdout")), "stderr": str(prefix.with_suffix(".stderr"))}


def records(path, marker):
    result = []
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith(marker + " "):
            row = {}
            for field in line.split()[1:]:
                key, value = field.split("=", 1)
                row[key] = int(value) if value.isdigit() else value
            result.append(row)
    return result


def compile_probe(compiler, source, obj, prefix):
    # Allocator-affecting flag is LAST: the documented last-option-wins rule.
    argv = [compiler, "cc", "-O0", "-g0", "-fverify-codegen", "-fno-machine-fallback", "-c", source, "-o", obj, "-fregister-allocator=quality"]
    return command(argv, prefix)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--trusted", type=Path, help="Optional unmodified trusted-producer baseline for observer object parity")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host-cc", default="clang")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.baseline = args.baseline.resolve()
    args.candidate = args.candidate.resolve()
    if args.trusted is not None:
        args.trusted = args.trusted.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = ROOT / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    for name, digest in manifest["sha256"].items():
        if sha256(ROOT / name) != digest:
            parser.error("frozen fixture changed: " + name)
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout.strip()
    report = {"revision": revision, "platform": platform.platform(), "evidence_class": "diagnostic identity/execution, no timing acceptance",
              "manifest_sha256": sha256(manifest_path), "policy": manifest["policy"], "commands": [], "fixtures": [],
              "compilers": {"baseline": {"path": str(args.baseline), "sha256": sha256(args.baseline)},
                            "candidate": {"path": str(args.candidate), "sha256": sha256(args.candidate)}},
              "unrun": ["trusted uninstrumented A/B compiler throughput", "9700X performance acceptance", "self-built compiler performance", "self-host fixed point (external build-driver gate)"]}
    version = command([args.host_cc, "--version"], args.output / "host-version")
    report["commands"].append(version)
    if args.trusted is not None:
        report["compilers"]["trusted"] = {"path": str(args.trusted), "sha256": sha256(args.trusted)}
    all_passed = version["returncode"] == 0
    for fixture in manifest["fixtures"]:
        source = ROOT / fixture["path"]
        stem = fixture["path"].replace("/", "-").replace(".c", "")
        out = args.output / stem
        out.mkdir(exist_ok=True)
        row = {"fixture": fixture["path"], "split": fixture["split"], "source_sha256": sha256(source), "observer": {}}
        objects = {}
        for variant, compiler in (("baseline", args.baseline), ("candidate", args.candidate)):
            obj = out / (variant + ".o")
            record = compile_probe(compiler, source, obj, out / (variant + "-compile"))
            report["commands"].append(record)
            row[variant + "_compile_ok"] = record["returncode"] == 0 and obj.exists() and obj.stat().st_size > 0
            combined = Path(record["stdout"]).read_text(errors="replace") + Path(record["stderr"]).read_text(errors="replace")
            (out / (variant + "-combined.log")).write_text(combined)
            diag = records(out / (variant + "-combined.log"), "EFFORT_DIAGNOSTIC")
            heaps = records(out / (variant + "-combined.log"), "EFFORT_HEAP")
            row["observer"][variant] = {"functions": diag, "heaps": heaps}
            if row[variant + "_compile_ok"]:
                objects[variant] = obj
                row[variant + "_object_sha256"] = sha256(obj)
                row[variant + "_object_bytes"] = obj.stat().st_size
        row["object_identity"] = len(objects) == 2 and objects["baseline"].read_bytes() == objects["candidate"].read_bytes()
        if args.trusted is not None:
            trusted_obj = out / "trusted.o"
            trusted_compile = compile_probe(args.trusted, source, trusted_obj, out / "trusted-compile")
            report["commands"].append(trusted_compile)
            row["trusted_compile_ok"] = trusted_compile["returncode"] == 0 and trusted_obj.exists() and trusted_obj.stat().st_size > 0
            row["trusted_observer_object_identity"] = row["trusted_compile_ok"] and "baseline" in objects and trusted_obj.read_bytes() == objects["baseline"].read_bytes()
            if row["trusted_compile_ok"]:
                row["trusted_object_sha256"] = sha256(trusted_obj)
        baseline_diag = row["observer"]["baseline"]["functions"]
        candidate_diag = row["observer"]["candidate"]["functions"]
        same_facts = len(baseline_diag) == len(candidate_diag) and all(
            all(a[key] == b[key] for key in ("rows", "values", "valid", "spills", "reloads", "eligible", "interval_size"))
            for a, b in zip(baseline_diag, candidate_diag))
        row["diagnostic_facts_identity"] = same_facts
        row["observed_functions"] = len(baseline_diag)
        row["eligible_functions"] = sum(record["eligible"] for record in baseline_diag)
        row["candidate_skipped_functions"] = sum(record["skip"] for record in candidate_diag)
        row["avoided_requested_payload_bytes"] = sum(record["avoided_payload"] for record in candidate_diag)
        eligible_heaps = [record for record in row["observer"]["baseline"]["heaps"] if record["eligible"]]
        row["baseline_eligible_heaps_empty"] = len(eligible_heaps) == row["eligible_functions"] and all(record["candidates"] == 0 for record in eligible_heaps)
        row["candidate_skips_match_rule"] = all(record["skip"] == record["eligible"] for record in candidate_diag)
        outputs = {}
        reference = out / "reference.o"
        ref_compile = command([args.host_cc, "-O0", "-g0", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-c", source, "-o", reference], out / "reference-compile")
        report["commands"].append(ref_compile)
        if ref_compile["returncode"] == 0:
            objects["reference"] = reference
        for variant, obj in objects.items():
            executable = out / (variant + ".exe")
            linked = command([args.host_cc, "-O0", ROOT / "driver.c", obj, "-o", executable], out / (variant + "-link"))
            report["commands"].append(linked)
            if linked["returncode"] == 0:
                ran = command([executable], out / (variant + "-run"))
                report["commands"].append(ran)
                if ran["returncode"] == 0:
                    outputs[variant] = Path(ran["stdout"]).read_bytes()
        row["independent_execution_identity"] = len(outputs) == 3 and outputs["baseline"] == outputs["candidate"] == outputs["reference"]
        row["passed"] = row["object_identity"] and row["independent_execution_identity"] and same_facts and row["baseline_eligible_heaps_empty"] and row["candidate_skips_match_rule"]
        if args.trusted is not None:
            row["passed"] = row["passed"] and row["trusted_observer_object_identity"]
        all_passed = all_passed and row["passed"]
        report["fixtures"].append(row)
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{fixture['split']} {fixture['path']}: passed={row['passed']} eligible={row['eligible_functions']}/{row['observed_functions']} avoided-payload={row['avoided_requested_payload_bytes']}", flush=True)
    # A zero observer denominator cannot establish the proposed skip mechanism.
    report["mechanism_observed"] = sum(row["eligible_functions"] for row in report["fixtures"]) > 0
    report["passed"] = all_passed and report["mechanism_observed"]
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
