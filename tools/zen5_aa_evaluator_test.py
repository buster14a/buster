"""Synthetic offline fixtures for the #426 A/A policy evaluator.

Every bundle here is fabricated in a temporary directory in the
zen5-calibration-v1 result layout; none is host evidence.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any, Callable

import zen5_aa_evaluator as evaluator
from zen5_aa_noise import canonical_bytes, observation_reasons, sha256_bytes, write_json
from zen5_aa_noise_test import synthetic_capture as aa_capture
from zen5_build_control_test import synthetic_capture as build_capture
from zen5_calibration_handoff import freeze
from zen5_host_qualification_test import synthetic_result
from zen5_qualification_common import load_manifest

RECIPE = evaluator.RECIPE
PROFILE = "7" * 64
COMMON = ("repository", "environment_fingerprint_sha256", "host_qualification_sha256",
          "source_identity_sha256", "expected_output_sha256")


def synthetic_pmu() -> bytes:
    manifest, digest = load_manifest()
    record = synthetic_result(manifest, digest)
    record["repository"].update(revision="b" * 40, tree="c" * 40)
    return canonical_bytes(record)


def attempt_documents(job: str, attempt: str, pmu: bytes, wall_drift: int) -> tuple[dict[str, Any], dict[str, Any]]:
    fingerprint = json.loads(pmu)["environment_fingerprint_sha256"]
    captures = {"immutable": aa_capture(), "same-root-rebuild": build_capture("same-root-rebuild"),
                "cross-root": build_capture("cross-root")}
    aa = captures["immutable"]
    aa.update(environment_fingerprint_sha256=fingerprint, host_qualification_sha256=sha256_bytes(pmu))
    # Deterministic per-job RSS offsets keep pair centers varied (a constant
    # series has an undefined lag-one correlation) without any attempt trend.
    offset = int(hashlib.sha256(job.encode()).hexdigest()[:8], 16) % 5
    for kind, capture in captures.items():
        if kind != "immutable":
            for field in COMMON:
                capture[field] = deepcopy(aa[field])
            for build in capture["builds"].values():
                build.update(source_revision=aa["repository"]["revision"], source_tree=aa["repository"]["tree"],
                             source_identity_sha256=aa["source_identity_sha256"])
        capture.update(recipe=RECIPE, ab_authorized=False, job=job, attempt=attempt)
        for observation in capture["observations"]:
            step = 4096 * ((observation["sequence"] * 3 + offset) % 7)
            for position in ("first", "second"):
                observation[f"{position}_peak_rss_bytes"] += step
                observation[f"{position}_wall_ns"] += wall_drift * observation["sequence"]
    spec = {**{key: deepcopy(aa[key]) for key in COMMON},
            "immutable_binary_sha256": aa["binary_paths"]["path0"]["sha256"],
            "minimum_interblock_gap_ns": aa["plan"]["minimum_interblock_gap_ns"],
            "controls": {kind: {"roots": {label: captures[kind]["builds"][label]["build_root"] for label in "AB"},
                                "binaries": {label: captures[kind]["builds"][label]["binary"]["sha256"]
                                             for label in "AB"}}
                         for kind in evaluator.KINDS}}
    plan = freeze(spec)
    for capture in captures.values():
        capture["predeclared_family_sha256"] = sha256_bytes(canonical_bytes(plan))
    return plan, captures


def write_bundle(root: Path, role: str, job: str, attempt: str = "1", *, pmu: bytes, wall_drift: int = 0,
                 mutate: Callable[[dict[str, Any], dict[str, Any]], None] | None = None,
                 manifest_update: dict[str, str] | None = None) -> dict[str, Any]:
    """Write one result tree (final manifest, BQ-BUNDLE-V1 index, plan, PMU, captures)."""
    name = f"job-{job}-attempt-{attempt}"
    directory = root / name
    plan, captures = attempt_documents(job, attempt, pmu, wall_drift)
    pmu_bytes = pmu
    if mutate is not None:
        pmu_document = json.loads(pmu)
        mutate(captures, pmu_document)
        pmu_bytes = canonical_bytes(pmu_document)
    files = {"zen5/plan.json": canonical_bytes(plan), "zen5/pmu/zen5-pmu-v1.json": pmu_bytes,
             f"{RECIPE}.plan.manifest": b"schema=1\nstage=plan\ntimed-children-started=0\n",
             **{f"zen5/captures/{kind}.json": canonical_bytes(capture) for kind, capture in captures.items()}}
    for relative, data in files.items():
        (directory / relative).parent.mkdir(parents=True, exist_ok=True)
        (directory / relative).write_bytes(data)
    ordered = sorted(files, key=lambda path: path.encode())
    index = (f"BQ-BUNDLE-V1\nentries={len(files)}\nbytes={sum(len(data) for data in files.values())}\n" +
             "".join(f"{sha256_bytes(files[path])} {len(files[path])} {path}\n" for path in ordered)).encode()
    (directory / f"{RECIPE}.bundle").write_bytes(index)
    digests = {kind: sha256_bytes(files[f"zen5/captures/{kind}.json"]) for kind in evaluator.CONTROLS}
    plan_digest = sha256_bytes(files["zen5/plan.json"])
    manifest = {
        "schema": "1", "recipe": RECIPE, "status": "succeeded", "stage": "complete", "process-result": "success",
        "reason": "", "job-id": job, "attempt-token": attempt, "workspace-root": "/srv/buster-bench/workspaces",
        "result-root": f"/srv/buster-bench/results/{name}", "base-revision": "b" * 40,
        "candidate-revision": "b" * 40, "source-tree": "c" * 40, "source-identity-sha256": "f" * 64,
        "profile-sha256": PROFILE, "budget-seconds": "2700", "elapsed-ns": "1", "plan-sha256": plan_digest,
        "plan-frozen-monotonic-ns": "1", "oracle-output-sha256": "a" * 64, "oracle-consistent": "true",
        "pmu-status": "pmu-qualified", "host-qualification-sha256": sha256_bytes(pmu_bytes),
        **{f"{kind}-capture-sha256": digests[kind] for kind in evaluator.CONTROLS},
        "trusted-builds": "5", "pairs": "360", "timed-children": "720", "ab-authorized": "false",
        "aa-decision": "not-evaluated", "bundle-sha256": sha256_bytes(index),
    }
    manifest.update(manifest_update or {})
    (directory / f"{RECIPE}.manifest").write_text("".join(f"{key}={value}\n" for key, value in manifest.items()))
    return {"role": role, "job_id": job, "attempt": attempt, "bundle": name,
            "trusted": {"plan_sha256": plan_digest, "captures": digests}}


def complete_protocol(pilots: list[dict[str, Any]]) -> dict[str, Any]:
    """A fully populated synthetic protocol; its values are test inputs, not reviewed limits."""
    protocol = evaluator.template_protocol()
    protocol["limits"] = {member: "10" for member in evaluator.MEMBERS}
    protocol["stationarity"] = {"alpha": "0.05", "permutations": 1999, "seed": 426}
    protocol["applicability"] = {"revision": "b" * 40, "tree": "c" * 40,
                                 "source_identity_sha256": "f" * 64, "profile_sha256": PROFILE}
    protocol["pilot_attempts"] = [{"job_id": entry["job_id"], "attempt": entry["attempt"]} for entry in pilots]
    protocol["current_job"]["equivalence_band"] = {"lower": "0.99", "upper": "1.01"}
    protocol["approval"] = {"approved_by": "synthetic-fixture", "reference": "offline self-test only"}
    return protocol


def ledger(entries: list[dict[str, Any]]) -> dict[str, Any]:
    return {"schema": evaluator.LEDGER_SCHEMA, "version": 1, "attempts": entries}


def refused(protocol: Any, attempts: Any, root: Path, fragment: str) -> None:
    try:
        evaluator.evaluate(protocol, attempts, root)
    except evaluator.EvaluatorError as error:
        assert fragment in str(error), (fragment, str(error))
    else:
        raise AssertionError(f"accepted a malformed input; expected {fragment!r}")


def single(root: Path, pmu: bytes, job: str, **options: Any) -> dict[str, Any]:
    entry = write_bundle(root, "confirmatory", job, pmu=pmu, **options)
    manifest, digest = load_manifest()
    return evaluator.attempt_record(entry, root, manifest, digest)


def invalid_slot(captures: dict[str, Any], _: dict[str, Any]) -> None:
    capture = captures["immutable"]
    observation = capture["observations"][7]
    observation["second_exit_status"] = 9
    observation["invalid_reasons"] = observation_reasons(observation, capture["expected_output_sha256"])
    observation["valid"] = False
    capture["capture_status"] = "invalid"
    capture["invalid_reasons"] = [f"observation 7: {reason}" for reason in observation["invalid_reasons"]]


def check_invalid_bundles(root: Path, pmu: bytes) -> None:
    good = single(root, pmu, "500")
    assert good["valid"] and all(value is not None for value in good["members"].values()), good["invalid_reasons"]
    drifting = single(root, pmu, "501", wall_drift=200)
    assert drifting["valid"], drifting["invalid_reasons"]
    for control in evaluator.CONTROLS:
        member = f"{control}.wall_time_ns.linear_drift"
        assert drifting["members"][member] > good["members"][member], member

    def expect(record: dict[str, Any], fragment: str) -> None:
        assert not record["valid"] and record["members"] is None, record
        assert any(fragment in reason for reason in record["invalid_reasons"]), (fragment, record["invalid_reasons"])

    expect(single(root, pmu, "502", manifest_update={"status": "failed", "stage": "captures",
                                                      "reason": "capture cross-root did not verify"}),
           "attempt failed at stage 'captures'")
    expect(single(root, pmu, "503", mutate=lambda captures, _: captures["same-root-rebuild"].update(ab_authorized=True)),
           "same-root-rebuild capture must carry ab_authorized=false")
    expect(single(root, pmu, "504", mutate=invalid_slot), "replay: immutable: observation 7")
    expect(single(root, pmu, "505", mutate=lambda _, record: record.update(qualification_status="invalid",
                                                                            invalid_reasons=["synthetic refusal"])),
           "PMU record is not pmu-qualified")
    expect(single(root, pmu, "506", manifest_update={"cross-root-capture-status": "invalid"}),
           "cross-root capture status is 'invalid'")
    expect(single(root, pmu, "507", manifest_update={"ab-authorized": "true"}), "ab-authorized is 'true'")
    tampered = write_bundle(root, "confirmatory", "508", pmu=pmu)
    path = root / tampered["bundle"] / "zen5/captures/immutable.json"
    path.write_bytes(path.read_bytes().replace(b'"first_wall_ns":1', b'"first_wall_ns":2', 1))
    manifest, digest = load_manifest()
    expect(evaluator.attempt_record(tampered, root, manifest, digest), "bundle file differs from its index")
    extra = write_bundle(root, "confirmatory", "509", pmu=pmu)
    (root / extra["bundle"] / "zen5/unlisted.txt").write_text("x")
    expect(evaluator.attempt_record(extra, root, manifest, digest), "unlisted file: zen5/unlisted.txt")
    missing = write_bundle(root, "confirmatory", "510", pmu=pmu)
    (root / missing["bundle"] / f"{RECIPE}.manifest").unlink()
    expect(evaluator.attempt_record(missing, root, manifest, digest), "the attempt is incomplete")
    wrong = write_bundle(root, "confirmatory", "511", pmu=pmu)
    wrong["trusted"]["plan_sha256"] = "0" * 64
    expect(evaluator.attempt_record(wrong, root, manifest, digest), "plan digest differs from the authenticated")
    wrong = write_bundle(root, "confirmatory", "512", pmu=pmu)
    wrong["trusted"] = None
    expect(evaluator.attempt_record(wrong, root, manifest, digest), "no authenticated service")
    relabeled = write_bundle(root, "confirmatory", "513", pmu=pmu)
    relabeled["job_id"] = "514"
    expect(evaluator.attempt_record(relabeled, root, manifest, digest), "job/attempt differs from the ledger")


def check_refusals(root: Path, pilots: list[dict[str, Any]], confirmatory: list[dict[str, Any]]) -> None:
    protocol = complete_protocol(pilots)
    refused(protocol, ledger(confirmatory[:2][::-1]), root, "not after its predecessor")
    refused(protocol, ledger([confirmatory[0], {**pilots[0], "job_id": "900"}]), root, "pilot follows")
    refused(protocol, ledger([{**confirmatory[0], "role": "baseline"}]), root, "role must be pilot or confirmatory")
    refused(protocol, {"schema": evaluator.LEDGER_SCHEMA, "version": 1, "attempts": []}, root, "nonempty")
    for key, value, fragment in (("quantile", "0.95", "protocol quantile"), ("family_size", 40, "family_size"),
                                 ("confirmatory_attempts", 63, "confirmatory_attempts"),
                                 ("family_alpha", "0.10", "family_alpha")):
        refused({**protocol, key: value}, ledger(confirmatory[:1]), root, fragment)
    changed = deepcopy(protocol)
    changed["stationarity"]["permutations"] = 999
    refused(changed, ledger(confirmatory[:1]), root, "cannot reach the family-corrected")
    changed = deepcopy(protocol)
    changed["current_job"]["pairs_per_round"] = 64
    refused(changed, ledger(confirmatory[:1]), root, "pairs_per_round=60")
    changed = deepcopy(protocol)
    changed["current_job"]["runtime_rows"] = "U<R"
    refused(changed, ledger(confirmatory[:1]), root, "runtime_rows=U=R")
    changed = deepcopy(protocol)
    del changed["limits"][evaluator.MEMBERS[5]]
    refused(changed, ledger(confirmatory[:1]), root, "exactly the 42 family members")
    changed = deepcopy(protocol)
    changed["limits"][evaluator.MEMBERS[0]] = "-1"
    refused(changed, ledger(confirmatory[:1]), root, "nonnegative decimal")
    changed = deepcopy(protocol)
    changed["current_job"]["equivalence_band"] = {"lower": "1.00", "upper": "1.02"}
    refused(changed, ledger(confirmatory[:1]), root, "0 < lower < 1 < upper")


def check_family(protocol: dict[str, Any], records: list[dict[str, Any]]) -> None:
    """Drift, serial dependence, limits, counts and scope on the retained records."""

    def status(changed_protocol: dict[str, Any], changed: list[dict[str, Any]], fragment: str, expected: str) -> None:
        policy = evaluator.build_policy(changed_protocol, changed)
        assert policy["status"] == expected, (expected, policy["status"], policy["reasons"])
        assert policy["ab_authorized"] is False
        assert any(fragment in reason for reason in policy["reasons"]), (fragment, policy["reasons"])

    pilots = [record for record in records if record["role"] == "pilot"]
    confirmatory = [record for record in records if record["role"] == "confirmatory"]
    changed = deepcopy(protocol)
    changed["limits"]["immutable.wall_time_ns.linear_drift"] = "0"
    status(changed, records, "immutable.wall_time_ns.linear_drift: upper bound", "inconclusive")

    member = "same-root-rebuild.wall_time_ns.path_effect"
    drifted = deepcopy(records)
    for index, record in enumerate(item for item in drifted if item["role"] == "confirmatory"):
        record["members"][member] = 0.001 * (index + 1)
    status(protocol, drifted, f"{member}: across-attempt trend", "inconclusive")

    serial = deepcopy(records)
    for index, record in enumerate(item for item in serial if item["role"] == "confirmatory"):
        record["members"][member] = 0.01 if (index // 8) % 2 == 0 else 0.02
    policy = evaluator.build_policy(protocol, serial)
    assert policy["status"] == "inconclusive"
    assert any(f"{member}: across-attempt serial" in reason for reason in policy["reasons"]), policy["reasons"]
    assert not any(f"{member}: across-attempt trend" in reason for reason in policy["reasons"])

    undefined = deepcopy(records)
    undefined[-1]["members"]["cross-root.peak_rss_bytes.serial_effect"] = None
    status(protocol, undefined, "cross-root.peak_rss_bytes.serial_effect: a confirmatory value is unavailable",
           "inconclusive")

    status(protocol, pilots + confirmatory[:10], "insufficient evidence: 10 of 64", "inconclusive")
    short = evaluator.build_policy(protocol, pilots + confirmatory[:10])
    assert short["evidence"]["sufficient"] is False and short["family"] is None
    assert all(record["members"] is None for record in short["attempts"] if record["role"] == "confirmatory")
    extra = deepcopy(confirmatory[-1])
    extra["job_id"] = "999"
    status(protocol, records + [extra], "exceed the fixed count 64", "invalid")
    broken = deepcopy(records)
    broken[5].update(valid=False, members=None, invalid_reasons=["synthetic invalid attempt"])
    status(protocol, broken, "is invalid and retained", "invalid")
    changed = deepcopy(protocol)
    changed["pilot_attempts"] = changed["pilot_attempts"][:1]
    status(changed, records, "differ from the protocol's declared pilot", "invalid")
    changed = deepcopy(protocol)
    changed["applicability"]["profile_sha256"] = "8" * 64
    status(changed, records, "profile_sha256 is outside the declared applicability", "unavailable")
    status(evaluator.template_protocol(), records, "protocol approval is unset", "unavailable")
    withheld = evaluator.build_policy(evaluator.template_protocol(), records)
    assert withheld["family"] is None and withheld["pilot_summary"][member]["count"] == len(pilots)

    table = {member: [record["members"][member] for record in drifted if record["role"] == "confirmatory"]}
    assert evaluator.stationarity(table, 199, 7) == evaluator.stationarity(table, 199, 7)
    assert evaluator.doubled_ranks([3.0, 1.0, 3.0, 2.0]) == [7, 2, 7, 4]


def run_self_test() -> int:
    assert evaluator.FAMILY_SIZE == 42 and len(set(evaluator.MEMBERS)) == 42
    assert evaluator.minimum_attempts() == 64
    assert evaluator.order_statistic_rank(63) is None and evaluator.order_statistic_rank(64) == 64
    assert evaluator.order_statistic_rank(132) == 129
    assert not evaluator.protocol_problems(evaluator.template_protocol())
    with tempfile.TemporaryDirectory(prefix="zen5-aa-evaluator-") as temporary:
        root = Path(temporary)
        pmu = synthetic_pmu()
        check_invalid_bundles(root, pmu)
        pilots = [write_bundle(root, "pilot", str(job), pmu=pmu) for job in (1, 2)]
        confirmatory = [write_bundle(root, "confirmatory", str(job), pmu=pmu) for job in range(10, 74)]
        check_refusals(root, pilots, confirmatory)

        unavailable = evaluator.evaluate(evaluator.template_protocol(), ledger(pilots), root)
        assert unavailable["status"] == "unavailable" and unavailable["family"] is None
        assert unavailable["evidence"]["pilot_valid"] == 2 and not unavailable["evidence"]["sufficient"]

        protocol = complete_protocol(pilots)
        policy = evaluator.evaluate(protocol, ledger(pilots + confirmatory), root)
        assert policy["status"] == "eligible", policy["reasons"]
        assert policy["family"]["rank"] == 64 and policy["evidence"]["sufficient"]
        assert policy["current_job_aa"]["pairs_per_round"] == 60 and policy["current_job_aa"]["runtime_rows"] == "U=R"
        check_family(protocol, [{key: value for key, value in record.items() if key != "index"}
                                for record in policy["attempts"]])

        protocol_path, ledger_path = root / "protocol.json", root / "ledger.json"
        policy_path, report_path = root / "out" / "policy.json", root / "out" / "report.md"
        write_json(protocol_path, protocol)
        write_json(ledger_path, ledger(pilots + confirmatory))
        tool = Path(__file__).with_name("zen5_aa_evaluator.py")

        def invoke(*arguments: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([sys.executable, "-B", str(tool), *arguments], text=True,
                                  capture_output=True, check=False)

        inputs = ("--protocol", str(protocol_path), "--ledger", str(ledger_path))
        run = invoke("evaluate", *inputs, "--policy-output", str(policy_path), "--report-output", str(report_path))
        digest = sha256_bytes(policy_path.read_bytes())
        assert run.returncode == 0 and run.stdout.strip() == f"eligible aa-policy-sha256={digest}", run
        # A separate process reproduces the in-process document byte for byte.
        assert policy_path.read_bytes() == canonical_bytes(policy)
        report = report_path.read_text()
        assert "Status: **eligible**" in report and "Pilot attempts (exploratory" in report and digest in report
        checked = invoke("verify", *inputs, "--policy", str(policy_path))
        assert checked.returncode == 0, checked
        policy_path.write_bytes(policy_path.read_bytes().replace(b'"eligible"', b'"eligiblE"', 1))
        assert invoke("verify", *inputs, "--policy", str(policy_path)).returncode == 2
        write_json(ledger_path, ledger(pilots + confirmatory[:3]))
        short = invoke("evaluate", *inputs, "--policy-output", str(policy_path), "--report-output", str(report_path))
        assert short.returncode == 1 and short.stdout.startswith("inconclusive "), short
        assert "INSUFFICIENT EVIDENCE" in report_path.read_text()
        template = invoke("template", "--output", str(root / "template.json"))
        assert template.returncode == 0 and evaluator.load_json(root / "template.json") == evaluator.template_protocol()
    print("zen5_aa_evaluator self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(run_self_test())
