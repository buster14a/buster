#!/usr/bin/env python3
"""Reproduce schema-2 validator eligibility and exercise the C projection.

This intentionally uses the repository's small ContractTests fixture. It proves
that the validator's row-bound applicability and skip evidence reaches the C
service projection; it does not stand in for an authenticated full census.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


REPOSITORY = Path(__file__).resolve().parents[2]
TOOLS = REPOSITORY / "tools"
sys.path.insert(0, str(TOOLS))

import native_retirement_contract_test as contract_test
import native_retirement_performance_binding as binding


EXPECTED_PROBE_OUTPUT = "VALIDATOR_ELIGIBILITY rows=192 eligible=160 skipped=32"
INAPPLICABLE_RECORD = (
    "tests/unit.c", "x86_64-apple-ios", "platform-inapplicable",
    "source-registration-test",
)
UNAVAILABLE_RECORD = (
    "tests/unit.c", "x86_64-unknown-linux-gnu", "unavailable", "missing-native-oracle",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run_validator(shards, output):
    command = [
        sys.executable,
        str(TOOLS / "native_retirement_contract.py"),
        "validate-shards",
        *(str(shard) for shard in shards),
        "--out", str(output),
        "--require-clean-candidate",
    ]
    result = subprocess.run(command, cwd=REPOSITORY, check=False,
                            capture_output=True, text=True)
    require(result.returncode == 0,
            "schema-2 validate-shards failed:\n" + result.stdout + result.stderr)
    require(output.is_file(), "schema-2 validate-shards did not write its report")
    try:
        report = json.loads(output.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"schema-2 validator report is unreadable: {error}") from error
    require(report.get("schema") == 2, "validator did not emit schema 2")
    require(report.get("require_clean_candidate") is True,
            "validator report does not retain the clean-candidate requirement")
    require(report.get("clean_candidate") is True,
            "fixture candidate rows are not clean")
    require(report.get("rows_validated") == 192,
            "fixture does not cover its complete 192-row matrix")
    require(report.get("applicability_skip_rows") ==
            list(range(16)) + list(range(128, 144)),
            "validator did not derive the source-ledger unavailable and platform skip sets")
    require(report.get("acceptance_failure_rows") == list(range(16)),
            "validator acceptance failures do not match the unavailable ledger rows")
    require(report.get("clean_acceptance") is False,
            "validator did not retain the unresolved unavailable acceptance state")
    return report


def independently_replay_python(root, report, shard):
    _row_fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    by_row = {}
    for classification in report["applicability_classes"]:
        for row in report["applicability_rows_by_class"][classification]:
            require(row not in by_row, "validator applicability classes overlap")
            by_row[row] = classification
    require(set(by_row) == set(range(len(census_rows))),
            "validator report is not a complete applicability partition")
    skip_rows = set(report["applicability_skip_rows"])

    projection_evidence = binding._check_validator_projection_evidence(
        root, report, census_rows, by_row, skip_rows)
    binding._replay_validator_report(root, report, projection_evidence)
    return projection_evidence


def write_profile(path, artifacts):
    keys = (
        ("support-declaration-sha256", "support"),
        ("census-inputs-sha256", "inputs"),
        ("census-rows-sha256", "rows"),
        ("census-manifest-sha256", "manifest"),
        ("validator-source-applicability-sha256", "source_applicability"),
        ("validator-report-sha256", "report"),
        ("validator-applicability-sha256", "applicability"),
        ("validator-skips-sha256", "skips"),
    )
    data = "".join(f"{key}={sha256(artifacts[name].read_bytes())}\n"
                   for key, name in keys).encode("ascii")
    if path.exists():
        path.chmod(stat.S_IMODE(path.stat().st_mode) | stat.S_IWUSR)
    path.write_bytes(data)
    path.chmod(0o444)


def make_read_only(*paths):
    for path in paths:
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode),
                f"probe evidence is not a regular file: {path.name}")
        require(info.st_nlink == 1, f"probe evidence has unexpected hard links: {path.name}")
        path.chmod(0o444)


def probe_result(probe, artifacts):
    command = [
        str(probe),
        str(artifacts["profile"]),
        str(artifacts["support"]),
        str(artifacts["source_applicability"]),
        str(artifacts["inputs"]),
        str(artifacts["rows"]),
        str(artifacts["manifest"]),
        str(artifacts["report"]),
        str(artifacts["applicability"]),
        str(artifacts["skips"]),
    ]
    return subprocess.run(command, cwd=REPOSITORY, check=False,
                          capture_output=True, text=True)


def expect_probe_success(probe, artifacts):
    result = probe_result(probe, artifacts)
    require(result.returncode == 0,
            "compiled C eligibility probe rejected the genuine fixture:\n" +
            result.stdout + result.stderr)
    require(result.stdout.strip() == EXPECTED_PROBE_OUTPUT,
            "compiled C eligibility probe returned an unexpected projection: " +
            result.stdout.strip())


def expect_tamper_rejected(probe, artifacts, path, replacement, label):
    original = path.read_bytes()
    changed = replacement(original)
    require(changed != original, f"{label} tamper did not change the evidence")
    original_mode = stat.S_IMODE(path.stat().st_mode)
    profile = artifacts["profile"]
    original_profile = profile.read_bytes()
    original_profile_mode = stat.S_IMODE(profile.stat().st_mode)
    try:
        path.chmod(original_mode | stat.S_IWUSR)
        path.write_bytes(changed)
        path.chmod(original_mode)
        # Re-pin the changed bytes so the probe must reject their contents or
        # row relationship instead of merely noticing a stale digest.
        write_profile(profile, artifacts)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted tampered {label} evidence")
    finally:
        path.chmod(original_mode | stat.S_IWUSR)
        path.write_bytes(original)
        path.chmod(original_mode)
        profile.chmod(original_profile_mode | stat.S_IWUSR)
        profile.write_bytes(original_profile)
        profile.chmod(original_profile_mode)


def replace_once(data, old, new, label):
    require(old in data, f"expected a {label} marker in fixture evidence")
    return data.replace(old, new, 1)


def mutate_report(data, mutation):
    try:
        report = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot mutate validator report JSON: {error}") from error
    mutation(report)
    return (json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("utf-8")


def mutate_applicability_cell(data, row_number, updates):
    lines = data.decode("ascii").splitlines()
    columns = lines[0].split("\t")
    positions = {name: index for index, name in enumerate(columns)}
    found = False
    for index in range(1, len(lines)):
        fields = lines[index].split("\t")
        if fields[positions["row"]] == str(row_number):
            for name, value in updates.items():
                fields[positions[name]] = value
            lines[index] = "\t".join(fields)
            found = True
            break
    require(found, f"applicability fixture has no row {row_number}")
    return ("\n".join(lines) + "\n").encode("ascii")


def rebind_applicability_digest(report_data, applicability_data):
    return mutate_report(report_data, lambda report:
        report.__setitem__("applicability_sha256", sha256(applicability_data)))


def mutate_manifest_gap_summary(data, row_numbers):
    properties = dict(line.split("=", 1) for line in data.decode("ascii").splitlines())
    properties["supported_gap_count"] = str(len(row_numbers))
    properties["supported_gap_sha256"] = contract_test.contract.canonical_rows_digest(row_numbers)
    return "".join(f"{key}={value}\n" for key, value in properties.items()).encode("ascii")


def expect_tamper_rejected_many(probe, artifacts, replacements, label):
    originals = {name: artifacts[name].read_bytes() for name in replacements}
    modes = {name: stat.S_IMODE(artifacts[name].stat().st_mode) for name in replacements}
    profile = artifacts["profile"]
    original_profile = profile.read_bytes()
    original_profile_mode = stat.S_IMODE(profile.stat().st_mode)
    try:
        for name, changed in replacements.items():
            path = artifacts[name]
            path.chmod(modes[name] | stat.S_IWUSR)
            path.write_bytes(changed)
            path.chmod(modes[name])
        write_profile(profile, artifacts)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted tampered {label} evidence")
    finally:
        for name, original in originals.items():
            path = artifacts[name]
            path.chmod(modes[name] | stat.S_IWUSR)
            path.write_bytes(original)
            path.chmod(modes[name])
        profile.chmod(original_profile_mode | stat.S_IWUSR)
        profile.write_bytes(original_profile)
        profile.chmod(original_profile_mode)


def expect_missing_profile_pin_rejected(probe, artifacts, key):
    profile = artifacts["profile"]
    original = profile.read_bytes()
    mode = stat.S_IMODE(profile.stat().st_mode)
    prefix = (key + "=").encode("ascii")
    require(original.count(prefix) == 1, f"expected one {key} profile pin")
    changed = b"".join(line for line in original.splitlines(keepends=True)
                        if not line.startswith(prefix))
    require(changed != original, f"removing {key} did not change the profile")
    try:
        profile.chmod(mode | stat.S_IWUSR)
        profile.write_bytes(changed)
        profile.chmod(mode)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted missing {key} profile pin")
    finally:
        profile.chmod(mode | stat.S_IWUSR)
        profile.write_bytes(original)
        profile.chmod(mode)


def execute(probe):
    require(probe.is_file() and os.access(probe, os.X_OK),
            f"compiled C probe is missing or not executable: {probe}")
    fixture = contract_test.ContractTests(methodName="runTest")
    fixture.setUp()
    try:
        fixture.install_applicability({INAPPLICABLE_RECORD, UNAVAILABLE_RECORD})
        report_path = fixture.root / "validator-report.json"
        report = run_validator(fixture.shards, report_path)
        projection_evidence = independently_replay_python(
            fixture.root, report, fixture.shards[0])

        expected_skips = set(range(16)) | set(range(128, 144))
        require(projection_evidence["artifacts"][1]["path"] == "applicability-skips.tsv",
                "independent Python replay did not bind the expected skip artifact")
        require(set(report["applicability_skip_rows"]) == expected_skips,
                "expected source-ledger unavailable and platform rows are not all skipped")
        require(report["applicability_counts"]["platform-inapplicable"] == 16,
                "fixture applicability projection does not contain exactly 16 platform rows")
        require(report["applicability_counts"]["unavailable"] == 16,
                "fixture applicability projection does not contain exactly 16 unavailable rows")

        shard = fixture.shards[0]
        artifacts = {
            "support": shard / "support-contract.tsv",
            "source_applicability": shard / "applicability-ledger.tsv",
            "inputs": shard / "inputs.tsv",
            "rows": shard / "rows.tsv",
            "manifest": shard / "manifest.txt",
            "report": report_path,
            "applicability": fixture.root / "applicability.tsv",
            "skips": fixture.root / "applicability-skips.tsv",
            "profile": fixture.root / "eligibility.profile",
        }
        write_profile(artifacts["profile"], artifacts)
        make_read_only(*(path for name, path in artifacts.items() if name != "profile"))

        expect_probe_success(probe, artifacts)
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: replace_once(data, b'"clean_candidate": true',
                                       b'"clean_candidate": false', "report"),
            "validator report")
        expect_tamper_rejected(
            probe, artifacts, artifacts["applicability"],
            lambda data: replace_once(data, b"platform-inapplicable",
                                       b"admitted-supported", "applicability"),
            "applicability")
        expect_tamper_rejected(
            probe, artifacts, artifacts["skips"],
            lambda data: replace_once(data, b"source-registration-test",
                                       b"source-registration-tesU", "skip"),
            "skip")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report["applicability_rows_by_class"]["platform-inapplicable"].__setitem__(0, 127)),
            "validator report class membership")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report["applicability_skip_rows"].pop(0)),
            "validator report skip set")
        for field in ("candidate_failure_rows", "direct_reference_failure_rows",
                      "reference_failure_rows", "fallback_defect_rows",
                      "telemetry_defect_rows", "execution_defect_rows",
                      "artifact_defect_rows", "unexpected_failure_rows",
                      "reference_supplement_sha256"):
            expect_tamper_rejected(
                probe, artifacts, artifacts["report"],
                lambda data, field=field: mutate_report(data, lambda report:
                    report[field].append(17)),
                f"nonempty {field}")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report.__setitem__("clean_acceptance", True)),
            "clean-acceptance summary")

        wrong_acceptance = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 17, {"acceptance_failure": "1"})
        wrong_acceptance_report = mutate_report(
            artifacts["report"].read_bytes(), lambda report:
                report["acceptance_failure_rows"].append(17))
        wrong_acceptance_report = rebind_applicability_digest(wrong_acceptance_report,
                                                              wrong_acceptance)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": wrong_acceptance, "report": wrong_acceptance_report},
            "acceptance failure outside the unavailable class")

        missing_unavailable_failure = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 0, {"acceptance_failure": "0"})
        missing_unavailable_report = mutate_report(
            artifacts["report"].read_bytes(), lambda report:
                report["acceptance_failure_rows"].remove(0))
        missing_unavailable_report = rebind_applicability_digest(
            missing_unavailable_report, missing_unavailable_failure)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": missing_unavailable_failure, "report": missing_unavailable_report},
            "missing acceptance failure for an unavailable row")

        absent_ledger_classification = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 17,
            {"applicability": "unavailable", "admission": "unavailable",
             "reason": "compile-obligation-not-admitted", "ownership": "admission",
             "acceptance_failure": "1"})
        def classify_unledgered_row_unavailable(report):
            for key in ("applicability_rows_by_class", "admission_rows_by_class"):
                report[key]["admitted-supported"].remove(17)
                report[key]["unavailable"].append(17)
                report[key]["unavailable"].sort()
            for key in ("applicability_counts", "admission_counts"):
                report[key]["admitted-supported"] -= 1
                report[key]["unavailable"] += 1
            report["acceptance_failure_rows"].append(17)
            report["acceptance_failure_rows"].sort()
        absent_ledger_report = mutate_report(artifacts["report"].read_bytes(),
                                            classify_unledgered_row_unavailable)
        absent_ledger_report = rebind_applicability_digest(absent_ledger_report,
                                                           absent_ledger_classification)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": absent_ledger_classification, "report": absent_ledger_report},
            "unledgered row reclassification")

        changed_manifest = mutate_manifest_gap_summary(artifacts["manifest"].read_bytes(), [17])
        changed_report = mutate_report(artifacts["report"].read_bytes(), lambda report: (
            report.__setitem__("supported_gap_rows", [17]),
            report.__setitem__("supported_gap_count", 1),
            report.__setitem__("supported_gap_sha256",
                               contract_test.contract.canonical_rows_digest([17])),
            report.__setitem__("manifest_identity_sha256",
                contract_test.contract.manifest_identity_digest(
                    dict(line.split("=", 1) for line in changed_manifest.decode("ascii").splitlines())))))
        expect_tamper_rejected_many(probe, artifacts,
            {"manifest": changed_manifest, "report": changed_report}, "supported-gap summary")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report: (
                report.__setitem__("residual_rows", 1),
                report.__setitem__("residual_truncated", True))),
            "nonempty residual summary")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report.__setitem__("residual_sha256", "0" * 64)),
            "residual digest")
        expect_missing_profile_pin_rejected(
            probe, artifacts, "validator-source-applicability-sha256")
        expect_probe_success(probe, artifacts)
    finally:
        fixture.tearDown()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path,
                        help="compiled retirement validator eligibility probe")
    arguments = parser.parse_args()
    execute(arguments.probe.resolve())
    print("schema-2 validator eligibility fixture passed "
          "(192 rows, 160 eligible, 32 skipped; positive and tamper probes)")


if __name__ == "__main__":
    main()
