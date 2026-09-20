"""Offline replay for Ryzen 7 9700X host qualification records."""

from __future__ import annotations

from typing import Any

from zen5_qualification_common import (
    COMMIT_RE,
    RESULT_SCHEMA,
    SHA256_RE,
    QualificationError,
    canonical_bytes,
    environment_contract,
    group_expression,
    map_perf_entries,
    parse_perf_json,
    sha256_bytes,
    validate_hash_record,
    validate_host,
)

def validate_run_invocation(
    run: dict[str, Any], group: dict[str, Any], workload: dict[str, Any]
) -> list[str]:
    invocation = run.get("invocation")
    command = workload.get("argv")
    cpu = workload.get("selected_cpu")
    if not isinstance(invocation, list) or not all(isinstance(item, str) for item in invocation):
        return ["invocation must be a string list"]
    if not isinstance(command, list) or not command or not all(isinstance(item, str) for item in command):
        return ["workload argv must be a nonempty string list"]
    expected_prefix = ["perf", "stat", "--json-output", "--no-big-num", "--output"]
    if len(invocation) < 12 or invocation[:5] != expected_prefix:
        return ["invocation does not use the fixed perf-stat prefix"]
    if invocation[6:12] != ["-e", group_expression(group), "--", "taskset", "-c", str(cpu)]:
        return ["invocation does not use the fixed event group and CPU placement"]
    if invocation[12:] != command:
        return ["invocation workload differs from the declared argv"]
    if not invocation[5]:
        return ["invocation perf output path is empty"]
    return []


def validate_result(value: Any, manifest: dict[str, Any], manifest_sha256: str) -> list[str]:
    """Replay a record's structure without turning truthful invalidity into corruption.

    The returned list contains integrity/replay failures only. Physical qualification
    failures are recomputed and compared with ``invalid_reasons``; a record that
    truthfully says ``invalid`` is therefore still independently replayable.
    """

    problems: list[str] = []
    derived_invalid: list[str] = []
    if not isinstance(value, dict) or value.get("schema") != RESULT_SCHEMA:
        return [f"result schema must be {RESULT_SCHEMA}"]
    if value.get("version") != 1:
        problems.append("result version must be 1")
    if value.get("event_manifest_sha256") != manifest_sha256:
        problems.append("event manifest digest mismatch")
    if value.get("purpose") != manifest.get("purpose"):
        problems.append("result purpose differs from the event manifest")
    if not isinstance(value.get("event_manifest"), str) or not value["event_manifest"]:
        problems.append("event_manifest path is malformed")
    if not SHA256_RE.fullmatch(str(value.get("tool_sha256"))):
        problems.append("tool_sha256 is malformed")

    repository = value.get("repository")
    if not isinstance(repository, dict):
        problems.append("repository identity must be an object")
    else:
        revision = repository.get("revision")
        tree = repository.get("tree")
        status = repository.get("status")
        if not COMMIT_RE.fullmatch(str(revision)):
            derived_invalid.append("repository revision is unavailable or malformed")
        if not COMMIT_RE.fullmatch(str(tree)):
            derived_invalid.append("repository tree is unavailable or malformed")
        if status is None:
            derived_invalid.append("repository checkout status is unavailable")
        elif not isinstance(status, str):
            problems.append("repository checkout status must be a string or null")
        elif status:
            derived_invalid.append("repository checkout is not clean")

    host = value.get("host")
    if not isinstance(host, dict):
        problems.append("host must be an object")
    else:
        try:
            derived_invalid.extend(validate_host(manifest, host))
        except (KeyError, TypeError, AttributeError):
            problems.append("host record is malformed")
        required_host_values: tuple[tuple[str, Any], ...] = (
            ("boot_id", host.get("boot_id")),
            ("kernel.release", host.get("kernel", {}).get("release") if isinstance(host.get("kernel"), dict) else None),
            ("perf_version", host.get("perf_version")),
            ("cpu.microcode", host.get("cpu", {}).get("microcode") if isinstance(host.get("cpu"), dict) else None),
            (
                "topology.thread_siblings_list",
                host.get("topology", {}).get("thread_siblings_list") if isinstance(host.get("topology"), dict) else None,
            ),
            ("topology.smt_active", host.get("topology", {}).get("smt_active") if isinstance(host.get("topology"), dict) else None),
            ("power_policy.boost", host.get("power_policy", {}).get("boost") if isinstance(host.get("power_policy"), dict) else None),
            (
                "power_policy.scaling_driver",
                host.get("power_policy", {}).get("scaling_driver") if isinstance(host.get("power_policy"), dict) else None,
            ),
            (
                "power_policy.scaling_governor",
                host.get("power_policy", {}).get("scaling_governor") if isinstance(host.get("power_policy"), dict) else None,
            ),
            (
                "power_policy.energy_performance_preference",
                host.get("power_policy", {}).get("energy_performance_preference")
                if isinstance(host.get("power_policy"), dict)
                else None,
            ),
            (
                "firmware.bios_version",
                host.get("firmware", {}).get("bios_version") if isinstance(host.get("firmware"), dict) else None,
            ),
            (
                "memory.mem_total_bytes",
                host.get("memory", {}).get("mem_total_bytes") if isinstance(host.get("memory"), dict) else None,
            ),
        )
        for name, fact in required_host_values:
            if fact is None:
                derived_invalid.append(f"required host fact unavailable: {name}")

    contract = value.get("environment_contract")
    fingerprint = value.get("environment_fingerprint_sha256")
    if not isinstance(contract, dict) or not SHA256_RE.fullmatch(str(fingerprint)):
        problems.append("environment contract/fingerprint is malformed")
    elif sha256_bytes(canonical_bytes(contract)) != fingerprint:
        problems.append("environment fingerprint mismatch")
    elif isinstance(host, dict):
        external = contract.get("external_environment_inputs")
        if not isinstance(external, list):
            problems.append("environment external inputs must be a list")
        else:
            for index, record in enumerate(external):
                problems.extend(
                    f"environment external input[{index}]: {problem}"
                    for problem in validate_hash_record(record, allow_unavailable=False)
                )
            if contract != environment_contract(host, external):
                problems.append("environment contract does not replay from the host record")

    workload = value.get("workload")
    if not isinstance(workload, dict):
        problems.append("workload must be an object")
        workload = {}
    else:
        if not isinstance(workload.get("working_directory"), str) or not workload["working_directory"]:
            problems.append("workload working_directory is malformed")
        if not isinstance(workload.get("selected_cpu"), int) or isinstance(workload.get("selected_cpu"), bool):
            problems.append("workload selected_cpu is malformed")
        elif isinstance(host, dict) and workload["selected_cpu"] != host.get("selected_cpu"):
            problems.append("workload selected_cpu differs from the host record")
        problems.extend(f"workload executable: {problem}" for problem in validate_hash_record(workload.get("executable"), allow_unavailable=False))
        before = workload.get("inputs_before")
        after = workload.get("inputs_after")
        if not isinstance(before, list) or not isinstance(after, list):
            problems.append("workload input identities must be lists")
        else:
            for index, record in enumerate(before):
                problems.extend(
                    f"workload inputs_before[{index}]: {problem}"
                    for problem in validate_hash_record(record, allow_unavailable=False)
                )
            for index, record in enumerate(after):
                problems.extend(
                    f"workload inputs_after[{index}]: {problem}"
                    for problem in validate_hash_record(record, allow_unavailable=False)
                )
            if before != after:
                derived_invalid.append("one or more immutable workload inputs changed during capture")
        outputs = workload.get("declared_output_artifacts")
        if not isinstance(outputs, list) or not all(isinstance(path, str) and path for path in outputs):
            problems.append("declared output artifacts must be a string list")

    expected_groups = {group["id"]: group for group in manifest["groups"]}
    groups = value.get("groups")
    if not isinstance(groups, list):
        problems.append("groups must be a list")
        groups = []
    actual_groups: dict[str, dict[str, Any]] = {}
    for group in groups:
        if not isinstance(group, dict) or not isinstance(group.get("id"), str):
            problems.append("malformed group result")
            continue
        if group["id"] in actual_groups:
            problems.append(f"duplicate group result {group['id']}")
        else:
            actual_groups[group["id"]] = group
    if set(actual_groups) != set(expected_groups):
        problems.append("result group set does not match the manifest")

    for group_id, definition in expected_groups.items():
        result_group = actual_groups.get(group_id)
        if result_group is None:
            continue
        if result_group.get("definition") != definition:
            problems.append(f"{group_id}: embedded event definition mismatch")
        runs = result_group.get("runs")
        if not isinstance(runs, list) or len(runs) != manifest["repeats"]:
            problems.append(f"{group_id}: expected {manifest['repeats']} runs")
            continue
        for expected_repeat, run in enumerate(runs):
            prefix = f"{group_id} repeat {expected_repeat}"
            if not isinstance(run, dict) or run.get("repeat") != expected_repeat:
                problems.append(f"{group_id}: malformed repeat {expected_repeat}")
                continue
            problems.extend(f"{prefix}: {problem}" for problem in validate_run_invocation(run, definition, workload))
            expected_run_invalid: list[str] = []
            raw_perf = run.get("perf_json")
            replay: dict[str, Any] = {}
            if raw_perf is None:
                if run.get("perf_json_sha256") is not None:
                    problems.append(f"{prefix}: unavailable perf JSON has a digest")
                expected_run_invalid.append("perf output file is unavailable")
            elif not isinstance(raw_perf, str):
                problems.append(f"{prefix}: perf_json must be a string or null")
            else:
                if sha256_bytes(raw_perf.encode("utf-8")) != run.get("perf_json_sha256"):
                    problems.append(f"{prefix}: perf_json digest mismatch")
                try:
                    entries = parse_perf_json(raw_perf)
                    replay, replay_problems = map_perf_entries(
                        definition, entries, float(manifest["minimum_running_fraction"])
                    )
                    expected_run_invalid.extend(replay_problems)
                except QualificationError as error:
                    expected_run_invalid.append(str(error))
            if replay != run.get("observations"):
                problems.append(f"{prefix}: observation replay mismatch")

            for stream in ("stdout", "stderr"):
                size = run.get(f"{stream}_size")
                digest = run.get(f"{stream}_sha256")
                if not isinstance(size, int) or isinstance(size, bool) or size < 0:
                    problems.append(f"{prefix}: {stream}_size is malformed")
                if not SHA256_RE.fullmatch(str(digest)):
                    problems.append(f"{prefix}: {stream}_sha256 is malformed")

            artifacts = run.get("output_artifacts")
            if not isinstance(artifacts, list):
                problems.append(f"{prefix}: output_artifacts must be a list")
                artifacts = []
            declared_output_count = len(workload.get("declared_output_artifacts", [])) if isinstance(workload, dict) else 0
            if len(artifacts) != declared_output_count:
                problems.append(f"{prefix}: output artifact count differs from the declaration")
            for index, artifact in enumerate(artifacts):
                identity_problems = validate_hash_record(artifact, allow_unavailable=True)
                problems.extend(f"{prefix}: output_artifacts[{index}]: {problem}" for problem in identity_problems)
                if isinstance(artifact, dict) and artifact.get("sha256") is None and isinstance(artifact.get("path"), str):
                    expected_run_invalid.append(f"declared output artifact unavailable: {artifact['path']}")

            status = run.get("exit_status")
            if not isinstance(status, int) or isinstance(status, bool):
                problems.append(f"{prefix}: exit_status is malformed")
            elif status != 0:
                expected_run_invalid.append(f"perf/workload exited with status {status}")

            declared_run_invalid = run.get("invalid_reasons")
            if declared_run_invalid != expected_run_invalid:
                problems.append(f"{prefix}: invalid_reasons do not replay")
            if run.get("valid") is not (not expected_run_invalid):
                problems.append(f"{prefix}: valid flag does not replay")
            derived_invalid.extend(f"{prefix}: {reason}" for reason in expected_run_invalid)

    ibs = value.get("optional_attribution")
    expected_ibs = {item["id"]: item for item in manifest.get("optional_attribution", [])}
    if not isinstance(ibs, list) or len(ibs) != len(expected_ibs):
        problems.append("optional IBS attribution inventory is incomplete")
        ibs = []
    actual_ibs: set[str] = set()
    for observation in ibs:
        if not isinstance(observation, dict) or observation.get("id") not in expected_ibs:
            problems.append("optional IBS attribution inventory contains an unknown entry")
            continue
        item_id = observation["id"]
        if item_id in actual_ibs:
            problems.append(f"duplicate optional IBS attribution entry {item_id}")
        actual_ibs.add(item_id)
        definition = expected_ibs[item_id]
        if observation.get("required") is not False or observation.get("count") is not None:
            problems.append(f"{item_id}: IBS inventory must remain optional and unmeasured")
        if observation.get("running_fraction") is not None:
            problems.append(f"{item_id}: unmeasured IBS running fraction must be null")
        if observation.get("measurement_status") != "not-requested":
            problems.append(f"{item_id}: IBS measurement status must be not-requested")
        if observation.get("scope") != definition.get("scope") or observation.get("semantics") != definition.get("semantics"):
            problems.append(f"{item_id}: IBS definition mismatch")

    if value.get("ordinary_timing_trials_separate") is not True:
        problems.append("ordinary timing trials must remain separate")
    if value.get("statistical_decision") != "not-evaluated":
        problems.append("PMU qualification must not contain a statistical timing decision")

    declared = value.get("invalid_reasons")
    if not isinstance(declared, list) or not all(isinstance(problem, str) and problem for problem in declared):
        problems.append("invalid_reasons must be a nonempty-string list")
        declared = []
    elif len(declared) != len(set(declared)):
        problems.append("invalid_reasons contains duplicates")
    missing_derived = [reason for reason in derived_invalid if reason not in declared]
    if missing_derived:
        problems.append("invalid_reasons omits replayed qualification failures: " + "; ".join(missing_derived))

    declared_status = value.get("qualification_status")
    if declared_status not in {"pmu-qualified", "invalid"}:
        problems.append("qualification_status must be pmu-qualified or invalid")
    elif declared_status == "pmu-qualified":
        if declared or derived_invalid:
            problems.append("pmu-qualified result contains qualification failures")
    elif not declared:
        problems.append("invalid result must retain at least one invalid reason")
    return problems


def compare_results(left: dict[str, Any], right: dict[str, Any]) -> dict[str, Any]:
    if left.get("schema") != RESULT_SCHEMA or right.get("schema") != RESULT_SCHEMA:
        raise QualificationError("compare inputs must use the host qualification schema")
    changes: list[dict[str, Any]] = []

    def walk(path: str, a: Any, b: Any) -> None:
        if isinstance(a, dict) and isinstance(b, dict):
            for key in sorted(set(a) | set(b)):
                walk(f"{path}.{key}" if path else key, a.get(key), b.get(key))
        elif a != b:
            changes.append({"path": path, "before": a, "after": b})

    walk("", left.get("environment_contract"), right.get("environment_contract"))
    return {
        "schema": "buster-zen5-host-requalification-diff-v1",
        "same_environment": not changes,
        "requalification_required": bool(changes),
        "changes": changes,
        "boot_changed": left.get("host", {}).get("boot_id") != right.get("host", {}).get("boot_id"),
        "before_fingerprint": left.get("environment_fingerprint_sha256"),
        "after_fingerprint": right.get("environment_fingerprint_sha256"),
    }


