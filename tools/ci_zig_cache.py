#!/usr/bin/env python3
"""Resolve and retain exact Zig archive-cache policy for Buster CI."""

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys

import ci_zig

CACHE_SCHEMA = "buster-zig-cache-evidence-v1"
CACHE_KEY_SCHEMA = "zig-archive-v1"
MODES = frozenset(("ordinary", "prime", "read"))
EVENTS = frozenset(("pull_request", "push", "merge_group", "workflow_dispatch"))
SHARDS = frozenset(("release", "checks"))
NAMESPACE_MAX_LENGTH = 48
NAMESPACE_PATTERN = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
IDENTITY_PATTERN = re.compile(r"[A-Za-z0-9_.-]+")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True)
class CachePolicy:
    mode: str
    namespace: str
    key: str
    save: bool
    require_hit: bool
    publication_proof_required: bool


def _require_identity(name, value):
    if not value or not IDENTITY_PATTERN.fullmatch(value):
        raise ValueError(f"{name} must be a non-empty cache-key identity token")


def validate_namespace(mode, namespace):
    if mode not in MODES:
        raise ValueError(f"unsupported Zig cache mode: {mode}")
    if mode == "ordinary":
        if namespace:
            raise ValueError("ordinary Zig cache mode requires an empty namespace")
        return
    if not namespace:
        raise ValueError(f"{mode} Zig cache mode requires a namespace")
    if len(namespace) > NAMESPACE_MAX_LENGTH:
        raise ValueError(f"Zig cache namespace exceeds {NAMESPACE_MAX_LENGTH} characters")
    if not NAMESPACE_PATTERN.fullmatch(namespace):
        raise ValueError(
            "Zig cache namespace must be lowercase alphanumeric words "
            "separated by single hyphens"
        )


def resolve_policy(event_name, ref, default_branch, mode, namespace, runner_os,
                   runner_arch, target, manifest_hash, shard):
    if event_name not in EVENTS:
        raise ValueError(f"unsupported GitHub event for Zig cache policy: {event_name}")
    if shard not in SHARDS:
        raise ValueError(f"unsupported desktop shard for Zig cache policy: {shard}")
    validate_namespace(mode, namespace)
    if mode != "ordinary" and event_name != "workflow_dispatch":
        raise ValueError(f"{mode} Zig cache mode is available only to workflow_dispatch")
    if not ref:
        raise ValueError("GitHub ref must be non-empty")
    if not default_branch:
        raise ValueError("default branch must be non-empty")
    _require_identity("runner OS", runner_os)
    _require_identity("runner architecture", runner_arch)
    if target not in ci_zig.TARGETS:
        raise ValueError(f"unsupported Zig cache target: {target}")
    if not SHA256_PATTERN.fullmatch(manifest_hash):
        raise ValueError("Zig manifest cache identity must be a lowercase SHA-256 value")

    base_key = f"{CACHE_KEY_SCHEMA}-{runner_os}-{runner_arch}-{target}-{manifest_hash}"
    key = base_key if mode == "ordinary" else f"{base_key}-cohort-{namespace}"
    save = False
    require_hit = False
    publication_proof_required = False
    if mode == "ordinary":
        save = event_name == "push" and ref == f"refs/heads/{default_branch}"
    elif mode == "prime":
        # Release is the sole writer for the key shared by a lane's two shards.
        save = shard == "release"
        publication_proof_required = save
    else:
        require_hit = True
    return CachePolicy(mode, namespace, key, save, require_hit,
                       publication_proof_required)


def parse_cache_hit(value):
    normalized = value.strip().lower()
    if normalized == "true":
        return True
    if normalized in ("", "false"):
        return False
    raise ValueError(f"invalid actions/cache cache-hit output: {value!r}")


def validate_restore_result(expected_key, cache_hit, primary_key, matched_key,
                            require_hit=False):
    hit = parse_cache_hit(cache_hit)
    if primary_key != expected_key:
        raise ValueError(
            f"cache primary key does not match the declared exact key: "
            f"{primary_key!r} != {expected_key!r}"
        )
    if hit:
        if matched_key != expected_key:
            raise ValueError(
                f"cache hit restored an unexpected key: {matched_key!r} != {expected_key!r}"
            )
    elif matched_key:
        raise ValueError(f"cache miss reported an unexpected matched key: {matched_key!r}")
    if require_hit and not hit:
        raise ValueError("read Zig cache mode requires an exact frozen-population hit")
    return hit


def require_restored_population(mode, expected_key, cache_hit, primary_key, matched_key):
    if mode not in MODES:
        raise ValueError(f"unsupported Zig cache mode: {mode}")
    return validate_restore_result(
        expected_key,
        cache_hit,
        primary_key,
        matched_key,
        require_hit=mode == "read",
    )


def _write_json(path, data):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    temporary.write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(destination)


def _append_outputs(path, values):
    with Path(path).open("a", encoding="utf-8", newline="\n") as stream:
        for name, value in values.items():
            stream.write(f"{name}={value}\n")


def create_evidence(policy, manifest, *, event_name, ref, default_branch,
                    repository, source_sha, run_id, run_attempt, shard,
                    runner_label, runner_os, runner_arch, target, manifest_hash):
    version, archive_sha256, archive_size = ci_zig.load_pin(manifest, target)
    return {
        "schema": CACHE_SCHEMA,
        "event": {
            "name": event_name,
            "ref": ref,
            "default_branch": default_branch,
            "repository": repository,
            "source_sha": source_sha,
            "run_id": run_id,
            "run_attempt": run_attempt,
        },
        "shard": shard,
        "runner": {
            "label": runner_label,
            "os": runner_os,
            "arch": runner_arch,
        },
        "zig": {
            "target": target,
            "version": version,
            "archive_sha256": archive_sha256,
            "archive_size": archive_size,
            "manifest_hash": manifest_hash,
        },
        "mode": policy.mode,
        "namespace": policy.namespace,
        "effective_key": policy.key,
        "key_components": {
            "schema": CACHE_KEY_SCHEMA,
            "runner_os": runner_os,
            "runner_arch": runner_arch,
            "zig_target": target,
            "manifest_hash": manifest_hash,
            "namespace": policy.namespace,
        },
        "policy": {
            "save_authorized": policy.save,
            "require_hit": policy.require_hit,
            "publication_proof_required": policy.publication_proof_required,
        },
        "restore": {
            "outcome": "pending",
            "cache_hit": "unknown",
            "primary_key": "",
            "matched_key": "",
        },
        "install": {"outcome": "pending", "archive_verified": False},
        "save": {
            "authorized": policy.save,
            "attempted": False,
            "step_outcome": "not-requested" if not policy.save else "pending",
        },
        "publication": {
            "state": "pending",
            "proof_restore_outcome": "not-run",
            "proof_cache_hit": "not-run",
            "proof_primary_key": "",
            "proof_matched_key": "",
            "archive_verified": False,
        },
        "usable_population": False,
    }


def resolve_command(arguments):
    policy = resolve_policy(
        arguments.event_name,
        arguments.ref,
        arguments.default_branch,
        arguments.mode,
        arguments.namespace,
        arguments.runner_os,
        arguments.runner_arch,
        arguments.target,
        arguments.manifest_hash,
        arguments.shard,
    )
    evidence = create_evidence(
        policy,
        arguments.manifest,
        event_name=arguments.event_name,
        ref=arguments.ref,
        default_branch=arguments.default_branch,
        repository=arguments.repository,
        source_sha=arguments.source_sha,
        run_id=arguments.run_id,
        run_attempt=arguments.run_attempt,
        shard=arguments.shard,
        runner_label=arguments.runner_label,
        runner_os=arguments.runner_os,
        runner_arch=arguments.runner_arch,
        target=arguments.target,
        manifest_hash=arguments.manifest_hash,
    )
    _write_json(arguments.evidence, evidence)
    _append_outputs(arguments.output, {
        "mode": policy.mode,
        "namespace": policy.namespace,
        "key": policy.key,
        "save": str(policy.save).lower(),
        "require_hit": str(policy.require_hit).lower(),
        "publication_proof_required": str(policy.publication_proof_required).lower(),
    })
    print(
        f"ZIG_CACHE_POLICY mode={policy.mode} namespace={policy.namespace or '-'} "
        f"key={policy.key} save={str(policy.save).lower()} "
        f"require_hit={str(policy.require_hit).lower()}"
    )


def _normalize_outcome(value):
    normalized = value.strip().lower()
    if normalized not in ("success", "failure", "cancelled", "skipped", "unknown"):
        return "unknown"
    return normalized


def _append_summary(path, evidence):
    if not path:
        return
    with Path(path).open("a", encoding="utf-8", newline="\n") as stream:
        stream.write("## Zig archive cache evidence\n\n")
        stream.write("| Field | Value |\n| --- | --- |\n")
        for label, value in (
                ("Source", f"`{evidence['event']['source_sha']}`"),
                ("Shard / target", f"{evidence['shard']} / {evidence['zig']['target']}"),
                ("Mode", evidence["mode"]),
                ("Namespace", evidence["namespace"] or "ordinary"),
                ("Exact key", f"`{evidence['effective_key']}`"),
                ("Initial restore", evidence["restore"]["cache_hit"]),
                ("Save step", evidence["save"]["step_outcome"]),
                ("Publication", evidence["publication"]["state"]),
                ("Usable population", str(evidence["usable_population"]).lower())):
            stream.write(f"| {label} | {value} |\n")
        stream.write("\n")


def _validate_manifest_identity(evidence, manifest):
    target = evidence["zig"]["target"]
    actual = ci_zig.load_pin(manifest, target)
    expected = (
        evidence["zig"]["version"],
        evidence["zig"]["archive_sha256"],
        evidence["zig"]["archive_size"],
    )
    if actual != expected:
        raise ValueError("Zig cache evidence no longer matches the pinned manifest")
    return actual[1]


def record_evidence(evidence_path, manifest, cache_directory, *,
                    restore_outcome, cache_hit, primary_key, matched_key,
                    install_outcome, save_outcome, proof_restore_outcome,
                    proof_cache_hit, proof_primary_key, proof_matched_key,
                    summary_path=None):
    path = Path(evidence_path)
    evidence = json.loads(path.read_text(encoding="utf-8"))
    if evidence.get("schema") != CACHE_SCHEMA:
        raise ValueError("missing or unsupported Zig cache evidence schema")
    expected_digest = _validate_manifest_identity(evidence, manifest)
    mode = evidence["mode"]
    expected_key = evidence["effective_key"]
    restore_outcome = _normalize_outcome(restore_outcome)
    install_outcome = _normalize_outcome(install_outcome)
    save_outcome = _normalize_outcome(save_outcome)
    proof_restore_outcome = _normalize_outcome(proof_restore_outcome)
    initial_hit = parse_cache_hit(cache_hit)
    initial_error = None
    if restore_outcome == "success":
        try:
            validate_restore_result(
                expected_key,
                cache_hit,
                primary_key,
                matched_key,
            )
        except ValueError as restore_error:
            initial_error = str(restore_error)

    evidence["restore"] = {
        "outcome": restore_outcome,
        "cache_hit": "hit" if initial_hit else "miss",
        "primary_key": primary_key,
        "matched_key": matched_key,
    }
    evidence["install"] = {
        "outcome": install_outcome,
        # ci_zig exits successfully only after digest and installed-version checks.
        "archive_verified": install_outcome == "success",
    }
    save_authorized = bool(evidence["policy"]["save_authorized"])
    save_requested = save_authorized and not initial_hit
    evidence["save"] = {
        "authorized": save_authorized,
        "attempted": save_requested and save_outcome not in ("skipped", "unknown"),
        "step_outcome": save_outcome if save_requested else "not-requested",
    }
    evidence["publication"] = {
        "state": "not-applicable",
        "proof_restore_outcome": proof_restore_outcome,
        "proof_cache_hit": "not-run",
        "proof_primary_key": proof_primary_key,
        "proof_matched_key": proof_matched_key,
        "archive_verified": False,
    }
    evidence["usable_population"] = False
    error = None

    if restore_outcome != "success":
        evidence["publication"]["state"] = "restore-failed"
        error = "Zig cache restore step did not succeed"
    elif initial_error:
        evidence["publication"]["state"] = "restore-evidence-invalid"
        error = initial_error
    elif mode == "read" and not initial_hit:
        evidence["publication"]["state"] = "frozen-population-miss"
        error = "read Zig cache mode did not restore its frozen exact key"
    elif install_outcome != "success":
        evidence["publication"]["state"] = "archive-not-verified"
        error = "Zig archive installation and verification did not succeed"
    elif initial_hit:
        evidence["publication"]["state"] = "existing-hit-verified"
        evidence["publication"]["archive_verified"] = True
        evidence["usable_population"] = True
    elif mode == "prime" and save_authorized:
        if save_outcome != "success":
            evidence["publication"]["state"] = "publication-step-failed"
            error = "prime Zig cache publication step did not succeed"
        elif proof_restore_outcome != "success":
            evidence["publication"]["state"] = "publication-unverified"
            error = "prime Zig cache publication proof restore did not succeed"
        else:
            proof_hit = parse_cache_hit(proof_cache_hit)
            evidence["publication"]["proof_cache_hit"] = "hit" if proof_hit else "miss"
            try:
                validate_restore_result(
                    expected_key,
                    proof_cache_hit,
                    proof_primary_key,
                    proof_matched_key,
                    require_hit=True,
                )
            except ValueError as proof_error:
                evidence["publication"]["state"] = "publication-unverified"
                error = str(proof_error)
            if error is None:
                try:
                    ci_zig.verify_archive(
                        Path(cache_directory) / "archive",
                        expected_digest,
                    )
                    evidence["publication"]["state"] = "published-and-verified"
                    evidence["publication"]["archive_verified"] = True
                    evidence["usable_population"] = True
                except (OSError, ValueError) as verify_error:
                    evidence["publication"]["state"] = "publication-corrupt"
                    error = str(verify_error)
    elif mode == "prime":
        evidence["publication"]["state"] = "non-owner-miss-not-published"
    elif mode == "ordinary" and save_authorized:
        if save_outcome == "success":
            evidence["publication"]["state"] = "ordinary-save-attempted"
        else:
            evidence["publication"]["state"] = "ordinary-save-step-failed"
            error = "ordinary Zig cache save step did not succeed"
    else:
        evidence["publication"]["state"] = "ordinary-miss-not-published"

    _write_json(path, evidence)
    _append_summary(summary_path, evidence)
    print(
        f"ZIG_CACHE_EVIDENCE mode={mode} namespace={evidence['namespace'] or '-'} "
        f"key={expected_key} restore={evidence['restore']['cache_hit']} "
        f"publication={evidence['publication']['state']} "
        f"usable={str(evidence['usable_population']).lower()}"
    )
    if error:
        raise ValueError(error)
    return evidence


def _add_common_identity(parser):
    parser.add_argument("--event-name", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--default-branch", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--namespace", default="")
    parser.add_argument("--runner-label", required=True)
    parser.add_argument("--runner-os", required=True)
    parser.add_argument("--runner-arch", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--manifest", default=".github/zig.json")
    parser.add_argument("--manifest-hash", required=True)
    parser.add_argument("--shard", required=True)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    resolve = commands.add_parser("resolve")
    _add_common_identity(resolve)
    resolve.add_argument("--repository", required=True)
    resolve.add_argument("--source-sha", required=True)
    resolve.add_argument("--run-id", required=True)
    resolve.add_argument("--run-attempt", required=True)
    resolve.add_argument("--output", required=True)
    resolve.add_argument("--evidence", required=True)

    check = commands.add_parser("check-restore")
    check.add_argument("--mode", required=True)
    check.add_argument("--expected-key", required=True)
    check.add_argument("--cache-hit", default="")
    check.add_argument("--primary-key", default="")
    check.add_argument("--matched-key", default="")

    record = commands.add_parser("record")
    record.add_argument("--evidence", required=True)
    record.add_argument("--manifest", default=".github/zig.json")
    record.add_argument("--cache-directory", required=True)
    record.add_argument("--restore-outcome", required=True)
    record.add_argument("--cache-hit", default="")
    record.add_argument("--primary-key", default="")
    record.add_argument("--matched-key", default="")
    record.add_argument("--install-outcome", required=True)
    record.add_argument("--save-outcome", default="unknown")
    record.add_argument("--proof-restore-outcome", default="unknown")
    record.add_argument("--proof-cache-hit", default="")
    record.add_argument("--proof-primary-key", default="")
    record.add_argument("--proof-matched-key", default="")
    record.add_argument("--summary")
    return parser


def main():
    arguments = build_parser().parse_args()
    try:
        if arguments.command == "resolve":
            resolve_command(arguments)
        elif arguments.command == "check-restore":
            hit = require_restored_population(
                arguments.mode,
                arguments.expected_key,
                arguments.cache_hit,
                arguments.primary_key,
                arguments.matched_key,
            )
            print(
                f"ZIG_CACHE_RESTORE mode={arguments.mode} "
                f"key={arguments.expected_key} result={'hit' if hit else 'miss'}"
            )
        else:
            record_evidence(
                arguments.evidence,
                arguments.manifest,
                arguments.cache_directory,
                restore_outcome=arguments.restore_outcome,
                cache_hit=arguments.cache_hit,
                primary_key=arguments.primary_key,
                matched_key=arguments.matched_key,
                install_outcome=arguments.install_outcome,
                save_outcome=arguments.save_outcome,
                proof_restore_outcome=arguments.proof_restore_outcome,
                proof_cache_hit=arguments.proof_cache_hit,
                proof_primary_key=arguments.proof_primary_key,
                proof_matched_key=arguments.proof_matched_key,
                summary_path=arguments.summary,
            )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Zig cache policy failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
