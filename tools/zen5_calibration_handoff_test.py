"""Exercise the offline #426 producer-to-consumer qualification handoff."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile

from zen5_aa_noise import canonical_bytes, sha256_bytes, sha256_file, write_json
from zen5_aa_noise_test import synthetic_capture as aa_capture
from zen5_build_control_test import synthetic_capture as build_capture
from zen5_calibration_handoff import freeze, replay


def run() -> None:
    aa = aa_capture()
    same = build_capture("same-root-rebuild")
    cross = build_capture("cross-root")
    # The fixture controls carry a separate build-source identity; align all
    # three inputs before freezing the shared, pre-observation family.
    for capture in (same, cross):
        for field in ("repository", "environment_fingerprint_sha256", "host_qualification_sha256",
                      "source_identity_sha256", "expected_output_sha256"):
            capture[field] = deepcopy(aa[field])
        for build in capture["builds"].values():
            build["source_revision"] = aa["repository"]["revision"]
            build["source_tree"] = aa["repository"]["tree"]
            build["source_identity_sha256"] = aa["source_identity_sha256"]
    spec = {
        **{key: deepcopy(aa[key]) for key in ("repository", "environment_fingerprint_sha256",
           "host_qualification_sha256", "source_identity_sha256", "expected_output_sha256")},
        "immutable_binary_sha256": aa["binary_paths"]["path0"]["sha256"],
        "minimum_interblock_gap_ns": aa["plan"]["minimum_interblock_gap_ns"],
        "controls": {
            kind: {
                "roots": {label: capture["builds"][label]["build_root"] for label in ("A", "B")},
                "binaries": {label: capture["builds"][label]["binary"]["sha256"] for label in ("A", "B")},
            }
            for kind, capture in (("same-root-rebuild", same), ("cross-root", cross))
        },
    }
    plan = freeze(spec)
    digest = sha256_bytes(canonical_bytes(plan))
    captures = {"immutable": aa, "same-root-rebuild": same, "cross-root": cross}
    for capture in captures.values():
        capture["predeclared_family_sha256"] = digest
    hashes = {name: sha256_bytes(canonical_bytes(capture)) for name, capture in captures.items()}
    report = replay(plan, digest, captures, hashes, hashes)
    assert report["status"] == "descriptive-complete" and not report["errors"], report
    assert report["ab_authorized"] is False and report["physical_admission"] == "not-evaluated"
    assert set(report["captures"]) == set(captures)

    assert replay(plan, "0" * 64, captures, hashes, hashes)["status"] == "invalid"
    assert replay(plan, digest, {"immutable": aa}, hashes, hashes)["status"] == "invalid"
    assert replay(plan, digest, captures, hashes, {})["status"] == "invalid"
    altered_hashes = dict(hashes)
    altered_hashes["immutable"] = "0" * 64
    assert replay(plan, digest, captures, hashes, altered_hashes)["status"] == "invalid"
    mutated = deepcopy(captures)
    mutated["cross-root"]["builds"]["B"]["binary"]["sha256"] = "0" * 64
    assert replay(plan, digest, mutated, hashes, hashes)["status"] == "invalid"
    mutated = deepcopy(captures)
    mutated["immutable"]["observations"][0]["first_wall_ns"] = 0
    assert replay(plan, digest, mutated, hashes, hashes)["status"] == "invalid"
    mutated = deepcopy(captures)
    mutated["same-root-rebuild"]["predeclared_family_sha256"] = "0" * 64
    assert replay(plan, digest, mutated, hashes, hashes)["status"] == "invalid"
    mutated = deepcopy(captures)
    mutated["cross-root"]["expected_output_sha256"] = "0" * 64
    assert replay(plan, digest, mutated, hashes, hashes)["status"] == "invalid"
    with tempfile.TemporaryDirectory(prefix="zen5-calibration-fixture-") as root:
        directory = Path(root)
        spec_file, plan_file, receipt_file, report_file = (
            directory / name for name in ("spec.json", "plan.json", "receipt.json", "report.json")
        )
        write_json(spec_file, spec)
        tool = Path(__file__).with_name("zen5_calibration_handoff.py")
        def invoke(*arguments: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([sys.executable, "-B", str(tool), *arguments],
                                  text=True, capture_output=True, check=False)
        frozen = invoke("freeze", str(spec_file), "--output", str(plan_file))
        assert frozen.returncode == 0 and frozen.stdout.strip() == digest, frozen.stderr
        paths = {name: directory / f"{name}.json" for name in captures}
        for name, capture in captures.items():
            write_json(paths[name], capture)
        trusted = {name: sha256_file(path) for name, path in paths.items()}
        write_json(receipt_file, trusted)
        command = ("replay", str(plan_file), "--trusted-plan-sha256", digest,
                   "--trusted-captures", str(receipt_file),
                   "--immutable", str(paths["immutable"]),
                   "--same-root-rebuild", str(paths["same-root-rebuild"]),
                   "--cross-root", str(paths["cross-root"]), "--output", str(report_file))
        checked = invoke(*command)
        assert checked.returncode == 0 and checked.stdout.strip() == "descriptive-complete", checked.stderr
        changed = deepcopy(aa)
        changed["observations"][0]["first_wall_ns"] += 1
        write_json(paths["immutable"], changed)
        checked = invoke(*command)
        assert checked.returncode == 2 and "invalid" in checked.stdout, checked.stderr
    print("zen5_calibration_handoff integration fixture passed")


if __name__ == "__main__":
    run()
