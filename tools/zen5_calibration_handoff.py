#!/usr/bin/env python3
"""Freeze and independently replay the three #426 calibration controls.

The service must publish the plan digest before the first timed observation and
authenticate its job/attempt and capture digests out of band. This offline
reader cannot authorize a physical run or an A/B transition.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import posixpath
import sys
from typing import Any

from zen5_aa_noise import (
    NoiseError, SHA256_RE, TOTAL_PAIRS, analyze_capture as analyze_aa,
    canonical_bytes, load_json, plan_problems, schedule_sha256, sha256_bytes,
    sha256_file, validate_capture as validate_aa, write_json,
)
from zen5_build_control import (
    analyze_capture as analyze_build, validate_capture as validate_build,
)

SCHEMA = "buster-zen5-calibration-family-v1"
REPORT_SCHEMA = "buster-zen5-calibration-handoff-v1"
PHASE_SCHEMA = "buster-zen5-calibration-phase-v1"
KINDS = ("same-root-rebuild", "cross-root")
COMMON = ("repository", "environment_fingerprint_sha256", "host_qualification_sha256",
          "source_identity_sha256", "expected_output_sha256")


def plan_errors(plan: Any) -> list[str]:
    if not isinstance(plan, dict) or plan.get("schema") != SCHEMA or plan.get("version") != 1:
        return ["calibration family schema/version differs"]
    errors: list[str] = []
    if set(plan) != {"schema", "version", *COMMON, "immutable_binary_sha256", "controls", "aa_plan"}:
        errors.append("calibration family fields differ")
    repository = plan.get("repository")
    if not isinstance(repository, dict) or set(repository) != {"revision", "tree", "status"} or (
        not all(isinstance(repository.get(key), str) and len(repository[key]) == 40
                and all(char in "0123456789abcdef" for char in repository[key])
                for key in ("revision", "tree")) or repository.get("status") != ""
    ):
        errors.append("clean repository revision/tree required")
    for field in (*COMMON[1:], "immutable_binary_sha256"):
        if not SHA256_RE.fullmatch(str(plan.get(field))):
            errors.append(f"{field} must be a SHA-256 digest")
    aa_plan = plan.get("aa_plan")
    if not isinstance(aa_plan, dict) or set(aa_plan) != {
        "rounds", "blocks_per_round", "pairs_per_block", "pairs_per_round",
        "total_pairs", "minimum_interblock_gap_ns", "schedule_sha256",
        "stopping_rule", "outlier_policy", "decision",
    }:
        errors.append("fixed A/A plan fields differ")
    errors.extend(plan_problems(aa_plan))
    controls = plan.get("controls")
    if not isinstance(controls, dict) or set(controls) != set(KINDS):
        errors.append("both predeclared same-root and cross-root controls required")
    else:
        for kind in KINDS:
            control = controls[kind]
            if not isinstance(control, dict) or set(control) != {"roots", "binaries"}:
                errors.append(f"{kind} identities malformed")
                continue
            roots, binaries = control["roots"], control["binaries"]
            if not isinstance(roots, dict) or set(roots) != {"A", "B"} or not all(
                isinstance(root, str) and root.startswith("/") and root != "/" and
                posixpath.normpath(root) == root
                for root in roots.values()
            ):
                errors.append(f"{kind} build roots malformed")
            elif (roots["A"] == roots["B"]) != (kind == "same-root-rebuild"):
                errors.append(f"{kind} build-root relationship differs")
            if not isinstance(binaries, dict) or set(binaries) != {"A", "B"} or not all(
                SHA256_RE.fullmatch(str(digest)) for digest in binaries.values()
            ):
                errors.append(f"{kind} binary digests malformed")
    return errors


def freeze(spec: dict[str, Any]) -> dict[str, Any]:
    plan = dict(spec)
    plan["schema"] = SCHEMA
    plan["version"] = 1
    plan["aa_plan"] = {
        "rounds": 2, "blocks_per_round": 4, "pairs_per_block": 15,
        "pairs_per_round": 60, "total_pairs": TOTAL_PAIRS,
        "minimum_interblock_gap_ns": spec.get("minimum_interblock_gap_ns"),
        "schedule_sha256": schedule_sha256(),
        "stopping_rule": "fixed-count-no-optional-stopping",
        "outlier_policy": "retain-all-no-deletion", "decision": "descriptive-only",
    }
    del plan["minimum_interblock_gap_ns"]
    errors = plan_errors(plan)
    if errors:
        raise NoiseError("; ".join(errors))
    return plan


def replay(plan: dict[str, Any], trusted_digest: str, captures: dict[str, dict[str, Any]],
           capture_digests: dict[str, str], trusted_captures: dict[str, str]) -> dict[str, Any]:
    errors = plan_errors(plan)
    actual_digest = sha256_bytes(canonical_bytes(plan))
    if not SHA256_RE.fullmatch(trusted_digest) or trusted_digest != actual_digest:
        errors.append("independent pre-sample plan digest differs")
    if not isinstance(trusted_captures, dict) or set(trusted_captures) != {"immutable", *KINDS}:
        errors.append("independent service capture digests missing")
        trusted_captures = {}
    analyses: dict[str, Any] = {}
    for key in ("immutable", *KINDS):
        capture = captures.get(key)
        if not isinstance(capture, dict):
            errors.append(f"{key} capture missing")
            continue
        if trusted_captures.get(key) != capture_digests.get(key):
            errors.append(f"{key}: independently authenticated capture digest differs")
        validator = validate_aa if key == "immutable" else validate_build
        structural, invalid = validator(capture)
        errors.extend(f"{key}: {item}" for item in structural)
        errors.extend(f"{key}: {item}" for item in invalid)
        for field in COMMON:
            if capture.get(field) != plan.get(field):
                errors.append(f"{key}: {field} differs from frozen family")
        if capture.get("plan") != plan.get("aa_plan"):
            errors.append(f"{key}: fixed A/A schedule differs from frozen family")
        if capture.get("predeclared_family_sha256") != actual_digest:
            errors.append(f"{key}: predeclared family digest differs")
        if key == "immutable":
            paths = capture.get("binary_paths")
            if not isinstance(paths, dict) or any(
                not isinstance(paths.get(path), dict) or
                paths[path].get("sha256") != plan.get("immutable_binary_sha256")
                for path in ("path0", "path1")
            ):
                errors.append("immutable: binary bytes differ from frozen family")
        else:
            if capture.get("control_kind") != key:
                errors.append(f"{key}: control kind differs")
            for label in ("A", "B"):
                builds = capture.get("builds")
                build = builds.get(label, {}) if isinstance(builds, dict) else {}
                if not isinstance(build, dict) or build.get("build_root") != plan.get("controls", {}).get(key, {}).get("roots", {}).get(label) or (
                    not isinstance(build.get("binary"), dict) or build["binary"].get("sha256") !=
                    plan.get("controls", {}).get(key, {}).get("binaries", {}).get(label)
                ):
                    errors.append(f"{key}: build {label} identity differs")
        if not structural and not invalid:
            try:
                analysis = (analyze_aa if key == "immutable" else analyze_build)(capture, capture_digests[key])
                if analysis["analysis_status"] != "descriptive-complete":
                    errors.append(f"{key}: analysis is {analysis['analysis_status']}")
                analyses[key] = {"capture_sha256": capture_digests[key],
                                 "analysis_sha256": sha256_bytes(canonical_bytes(analysis)),
                                 "status": analysis["analysis_status"]}
            except (NoiseError, KeyError, TypeError, ValueError) as error:
                errors.append(f"{key}: analysis failed: {error}")
    return {
        "schema": REPORT_SCHEMA, "version": 1, "plan_sha256": actual_digest,
        "captures": analyses, "status": "invalid" if errors else "descriptive-complete",
        "errors": errors, "physical_admission": "not-evaluated",
        "candidate_decision": "not-evaluated", "ab_authorized": False,
    }


def verify_service_phase(plan: Any, report: Any,
                         presample: Any, completion: Any, trusted: Any) -> dict[str, Any]:
    """Check a service phase handoff; never turn descriptive replay into A/B authority.

    ``trusted`` must be populated by the protected service's authenticated,
    private control channel, independently of the capture/result bundle. In
    particular its receipt digests and live lease facts cannot be read from the
    two receipts under examination. The service must establish pre-sample
    persistence before the first observation; timestamps in a bundle cannot.
    """
    errors: list[str] = plan_errors(plan)
    plan_digest = sha256_bytes(canonical_bytes(plan))
    if not isinstance(plan, dict):
        plan = {}
    identities = ("job_id", "attempt", "host_id", "boot_id", "lease_id",
                  "profile_sha256", "host_qualification_sha256")
    expected_trusted = {*identities, "host_qualification_state", "lease_state",
                        "presample_persisted_before_timing", "presample_sha256",
                        "completion_sha256"}
    expected_pre = {"schema", "version", "phase", "status", *identities, "plan_sha256"}
    expected_post = {*expected_pre, "presample_sha256", "capture_digests", "report_sha256"}
    if not isinstance(trusted, dict) or set(trusted) != expected_trusted:
        errors.append("independent service authority unavailable or malformed")
        trusted = {}
    for name, receipt, fields, phase, status in (
        ("presample", presample, expected_pre, "pre-sample", "committed"),
        ("completion", completion, expected_post, "aa-complete", "complete"),
    ):
        if not isinstance(receipt, dict) or set(receipt) != fields:
            errors.append(f"{name} receipt fields differ")
            continue
        if (receipt["schema"], receipt["phase"], receipt["status"]) != (PHASE_SCHEMA, phase, status) or (
            type(receipt["version"]) is not int or receipt["version"] != 1
        ):
            errors.append(f"{name} receipt phase/status differs")
        if type(receipt["attempt"]) is not int or receipt["attempt"] < 1:
            errors.append(f"{name} receipt attempt must be a positive integer")
        if any(receipt[field] != trusted.get(field) for field in identities):
            errors.append(f"{name} receipt job/host/lease identity differs")
        if receipt["plan_sha256"] != plan_digest:
            errors.append(f"{name} receipt frozen plan differs")
        if not SHA256_RE.fullmatch(str(trusted.get(f"{name}_sha256"))) or (
            sha256_bytes(canonical_bytes(receipt)) != trusted.get(f"{name}_sha256")
        ):
            errors.append(f"{name} authenticated digest differs")
    if not isinstance(trusted.get("job_id"), str) or not trusted["job_id"] or (
        not isinstance(trusted.get("attempt"), int) or isinstance(trusted.get("attempt"), bool)
        or trusted["attempt"] < 1
    ):
        errors.append("service job/attempt unavailable")
    if any(not isinstance(trusted.get(key), str) or not trusted[key] for key in ("host_id", "boot_id", "lease_id")):
        errors.append("service host/boot/lease unavailable")
    for field in ("profile_sha256", "host_qualification_sha256"):
        if not SHA256_RE.fullmatch(str(trusted.get(field))):
            errors.append(f"service {field} unavailable")
    if trusted.get("host_qualification_sha256") != plan.get("host_qualification_sha256"):
        errors.append("host qualification differs from frozen plan")
    if trusted.get("host_qualification_state") != "pmu-qualified":
        errors.append("service host qualification unavailable or denied")
    if trusted.get("lease_state") != "exclusive-live":
        errors.append("service exclusive lease unavailable or stale")
    if trusted.get("presample_persisted_before_timing") is not True:
        errors.append("service did not attest pre-sample persistence")
    expected_report = {"schema", "version", "plan_sha256", "captures", "status", "errors",
                       "physical_admission", "candidate_decision", "ab_authorized"}
    if not isinstance(report, dict) or set(report) != expected_report:
        errors.append("independent raw capture replay report fields differ")
        report = {}
    if (report.get("schema") != REPORT_SCHEMA or type(report.get("version")) is not int or
        report.get("version") != 1 or report.get("status") != "descriptive-complete" or
        report.get("errors") != [] or report.get("plan_sha256") != plan_digest or
        report.get("ab_authorized") is not False or
        report.get("physical_admission") != "not-evaluated" or
        report.get("candidate_decision") != "not-evaluated"):
        errors.append("independent raw capture replay is incomplete or invalid")
    captures = report.get("captures")
    if not isinstance(captures, dict) or set(captures) != {"immutable", *KINDS}:
        errors.append("independent replay capture inventory differs")
        captures = {}
    capture_digests: dict[str, str] = {}
    for key in ("immutable", *KINDS):
        item = captures.get(key)
        if not isinstance(item, dict) or set(item) != {"capture_sha256", "analysis_sha256", "status"} or (
            not SHA256_RE.fullmatch(str(item.get("capture_sha256"))) or
            not SHA256_RE.fullmatch(str(item.get("analysis_sha256"))) or
            item.get("status") != "descriptive-complete"
        ):
            errors.append(f"{key} independent replay result differs")
        else:
            capture_digests[key] = item["capture_sha256"]
    if isinstance(completion, dict) and set(completion) == expected_post:
        if completion["presample_sha256"] != trusted.get("presample_sha256"):
            errors.append("completion does not follow authenticated pre-sample receipt")
        if completion["report_sha256"] != sha256_bytes(canonical_bytes(report)):
            errors.append("completion does not bind independent replay")
        if len(capture_digests) != 3 or completion["capture_digests"] != capture_digests:
            errors.append("completion capture digests differ from independent replay")
    return {
        "schema": PHASE_SCHEMA, "version": 1,
        "status": "verified-descriptive" if not errors else "denied",
        "errors": errors, "ab_authorized": False,
        "aa_decision": "not-evaluated", "candidate_decision": "not-evaluated",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    predeclare = commands.add_parser("freeze")
    predeclare.add_argument("spec", type=Path)
    predeclare.add_argument("--output", type=Path, required=True)
    verify = commands.add_parser("replay")
    verify.add_argument("plan", type=Path)
    verify.add_argument("--trusted-plan-sha256", required=True)
    verify.add_argument("--trusted-captures", type=Path, required=True,
                        help="digest map obtained from authenticated service, never from the result bundle")
    for name in ("immutable", *KINDS):
        verify.add_argument(f"--{name}", type=Path, required=True)
    verify.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "freeze":
            plan = freeze(load_json(args.spec))
            write_json(args.output, plan)
            print(sha256_file(args.output))
        else:
            paths = {name: getattr(args, name.replace("-", "_")) for name in ("immutable", *KINDS)}
            captures = {name: load_json(path) for name, path in paths.items()}
            digests = {name: sha256_file(path) for name, path in paths.items()}
            report = replay(load_json(args.plan), args.trusted_plan_sha256, captures, digests,
                            load_json(args.trusted_captures))
            write_json(args.output, report)
            print(report["status"])
            if report["errors"]:
                return 2
        return 0
    except (NoiseError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
