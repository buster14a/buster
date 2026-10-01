#!/usr/bin/env python3
"""Offline acceptance evidence for #2119 and #2120; never changes CI policy.

qualify() joins archived GitHub job inventories, native phase journals and
coverage to independently checked per-row unit-test censuses. No prediction,
partial attempt, missing condition or smaller isolated test qualifies a run.

Usage: python3 tools/ci_checks_qualification.py CAMPAIGN.json
JSON campaign schema (all paths relative to the campaign, unless noted):
  {"schema":"buster-ci-checks-qualification-v1", "repository":"buster14a/buster",
   "samples":[{"variant":"combined-overlap|combined-all-builds|split-overlap",
     "run":REF, "conditions":REF, "desktops":[{"job":"Windows x86-64 checks",
       "coverage":REF, "result":REF, "phases":REF,
       "phase_directory":"retained/matrix-phases",
       "tests":[{"row_id":"exact coverage row ID", "manifest":REF, "observation":REF,
                 "log_sha256":"64 lowercase hex"}]}]}]}
REF = {"path":"retained/file.json", "sha256":"64 lowercase hex"}.
run is one complete run object from github_ci_time.py collect, including jobs
and steps (not the collector's outer runs array). Each variant needs exactly
three different complete first attempts, dispatched from its matching codex/
ci-checks-<variant> branch, all at the same immutable source/workflow revision.
Every desktop job needs its unchanged result/coverage/phase summaries plus the
complete matrix-phases directory. Native journals are replayed, not trusted
because their summary says complete. Their summary digest binds the replay.
tests contains one ci_unit_tests_measure.py sample manifest for every selected
runtime row, with a retained log (relative to that manifest) and its digest.
Each test also binds the native buster-desktop-unit-observation-v1 receipt at
<phase_directory>.parent/unit-observations/<taskID>/observation.json. Its adjacent
inventory.log/test.log bytes, binary hash, source/run identity and command must
match the same phase and sample; the independent inventory query is replayed.
The manifest log must be that exact retained test.log. Its elapsed_us must equal
the native test phase, and its toolchain identity is
{compiler,path_hash,identity,target,version} from that row's detected capability.
Its runner_image is {image_os,image_version,runner}, matching conditions below.
The native inventory includes disabled audits; modules and assertions must pass.

conditions is {"schema":"buster-ci-checks-conditions-v1", "run_id":123,
 "jobs":{"exact GitHub job name":{"job_id":456,"image_os":"windows25",
 "image_version":"20260928.194.1","runner":"windows-2025",
 "toolchains":{...},"caches":{"BUSTER_CI_ZIG_CACHE_HIT":"true",...}}}}.
It covers every executed job and records actual retained image/toolchain/cache
observations. The optional main-reuse decision may be skipped on dispatch: it
adds zero runner seconds and has no conditions entry. Empty toolchains/caches are
explicit only for jobs using none. Missing/unknown observations fail pending.
Split checks conditions map to their platform's combined checks role; all three
must match that role. Other job conditions must match across every sample.

Native phase journals report CPU time and peak RSS as unknown. This consumer
can meet or reject timing/census thresholds, but cannot accept either issue's
full resource/deadline/cleanup/reliability contract. Positive timing results
stay pending until actual resource observations and that review are retained.

SHA-256 binds retained bytes, not their origin: collect these records from the
actual run/artifacts. This consumer cannot authenticate manually invented API
or provenance records. It issues an offline measurement verdict, never closes
an issue, dispatches CI, changes defaults or substitutes for CI complete.
An empty samples array is a useful pending template; no fake valid runs ship.
Exit 1 = a complete timing contract rejected; 2 = qualification pending
(including positive timing without resource/reliability review). Output is JSON.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import ntpath
import os
from pathlib import Path
import posixpath
import re
import statistics
import sys

import ci_matrix_phases as phases
import ci_summary_core as coverage_tools
import ci_unit_tests_measure as units
import ci_unit_tests_campaign as unit_campaign
import github_ci_time as github

SCHEMA = "buster-ci-checks-qualification-v1"
VARIANTS = ("combined-overlap", "combined-all-builds", "split-overlap")
HASH = re.compile(r"[0-9a-f]{64}\Z")
COMMIT = re.compile(r"[0-9a-f]{40}\Z")
CAP_KEYS = ("compiler", "path_hash", "identity", "target", "version")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def retained(root, reference):
    require(isinstance(reference, dict) and HASH.fullmatch(str(reference.get("sha256", ""))), "missing retained-file digest")
    path = root / reference["path"]
    require(not path.is_symlink() and path.is_file(), f"missing regular retained file: {path}")
    require(hashlib.sha256(path.read_bytes()).hexdigest() == reference["sha256"], f"retained-file digest mismatch: {path}")
    return path


def record(root, reference):
    return phases.read(retained(root, reference))


def role(name):
    result = name
    for shard in github.SPLIT_CHECK_SHARDS:
        if name.endswith(" " + shard):
            result = name.rsplit(" ", 1)[0] + " checks"
    return result


def known(value):
    result = False
    if isinstance(value, dict):
        result = all(isinstance(k, str) and k and known(v) for k, v in value.items())
    elif isinstance(value, list):
        result = bool(value) and all(known(v) for v in value)
    elif type(value) is bool or type(value) is int:
        result = True
    elif isinstance(value, str):
        result = bool(value) and value.lower() not in ("unknown", "missing", "unavailable", "pending")
    return result


def skipped_reuse(job):
    return job.get("name") == github.MAIN_REUSE_JOB and job.get("status") == "completed" and job.get("conclusion") == "skipped" and job.get("run_attempt") == 1


def timing(run, variant):
    layout = "split" if variant == "split-overlap" else "combined"
    expected = Counter(github.combination_jobs(layout))
    jobs = run.get("jobs", [])
    actual = Counter(job.get("name") for job in jobs)
    if github.MAIN_REUSE_JOB in actual:
        expected[github.MAIN_REUSE_JOB] = 1
    require(actual == expected, "not the exact 21/27 required jobs plus optional main-reuse job")
    require(run.get("event") == "workflow_dispatch" and run.get("head_branch") == "codex/ci-checks-" + variant,
            "qualification dispatch branch/variant mismatch")
    require(run.get("path") == ".github/workflows/ci.yml" and COMMIT.fullmatch(str(run.get("head_sha", ""))) and
            COMMIT.fullmatch(str(run.get("workflow_blob_sha", ""))), "unresolved source/workflow")
    measured, reason = github.measure(run)
    require(reason is None, "ineligible GitHub attempt: " + str(reason))
    created = github.timestamp(run["created_at"])
    ends, starts, busy, queues, skipped = [], [], 0.0, {}, []
    require(len({job["id"] for job in jobs}) == len(jobs), "duplicate GitHub job ID")
    for job in jobs:
        if skipped_reuse(job):
            skipped.append(job["name"])
        else:
            require(job.get("status") == "completed" and job.get("conclusion") == "success" and job.get("run_attempt") == 1, "job failed, skipped, deferred or rerun")
            queued, start, end = (github.timestamp(job.get(key)) for key in ("created_at", "started_at", "completed_at"))
            require(all(t is not None for t in (queued, start, end)) and created <= queued <= start < end, "missing/non-monotonic GitHub queue timestamps")
            ends.append(end)
            starts.append(start)
            busy += (end - start).total_seconds()
            queues[job["name"]] = (start - queued).total_seconds()
    measured.update(elapsed_seconds=(max(ends) - created).total_seconds(), runner_seconds=busy,
                    initial_queue_seconds=(min(starts) - created).total_seconds(), job_queue_seconds=queues,
                    job_count=len(jobs), skipped_metadata_jobs=skipped)
    return measured


def conditions(root, reference, run):
    data = record(root, reference)
    require(data.get("schema") == "buster-ci-checks-conditions-v1" and str(data.get("run_id")) == str(run["id"]), "conditions run/schema mismatch")
    entries = data.get("jobs", {})
    executed = [job for job in run["jobs"] if not skipped_reuse(job)]
    require(set(entries) == {job["name"] for job in executed}, "missing exact-job image/toolchain/cache conditions")
    normalized = {}
    for job in executed:
        entry = entries[job["name"]]
        require(entry.get("job_id") == job["id"], "conditions job ID mismatch")
        require(all(known(entry.get(k)) for k in ("image_os", "image_version", "runner")), "missing runner image/version/label")
        require(entry["runner"] in job.get("labels", []), "conditions runner label differs from GitHub assignment")
        require(all(isinstance(entry.get(k), dict) and known(entry[k]) for k in ("toolchains", "caches")), "missing toolchain/cache condition map")
        require(entry["toolchains"] or job["name"] in ("CI complete", github.MAIN_REUSE_JOB), "missing compiler/toolchain observations")
        value = {k: entry[k] for k in ("image_os", "image_version", "runner", "toolchains", "caches")}
        key = role(job["name"])
        require(key not in normalized or normalized[key] == value, "split jobs have different runner/cache conditions")
        normalized[key] = value
    return entries, normalized


def phase_environment(summary, metadata):
    """Keep retained native observations when generic metadata is unknown."""
    environment = dict(summary.get("runner", {}))
    for name, value in metadata.items():
        retained_value = environment.get(name)
        if known(retained_value) and known(value):
            require(retained_value == value, "conflicting known phase metadata: " + name)
        if known(value) or not known(retained_value):
            environment[name] = value
    return environment


def policy_rows(coverage, environment):
    """Bind the archived census to the independent production lane contract."""
    identity = coverage["identity"]
    expected = coverage.get("expected", [])
    rows = {row["id"]: row for row in expected}
    require(rows and len(rows) == len(expected), "empty/duplicate policy row census")
    for row in expected:
        require(row.get("compiler") in ("cl", "clang", "gcc", "zig") and row.get("configuration") in ("Debug", "Release"),
                "unsupported policy compiler/configuration")
        require(all(type(row.get(k)) is bool for k in ("optimize", "sanitize", "fuzz", "unity")), "malformed policy row booleans")
        require(row["optimize"] == (row["configuration"] == "Release") and
                row["unity"] == (row["compiler"] == "clang" and not row["sanitize"] and row["optimize"]),
                "policy optimization/unity differs from configuration")
        require(row.get("state") in ("required", "excluded") and isinstance(row.get("exclusion"), str) and
                ((row["state"] == "excluded") == bool(row["exclusion"])), "invalid exclusion census")
        execution = "none" if row["state"] == "excluded" else "runtime" if row["compiler"] == "clang" else "compile-link"
        require(row.get("execution") == execution, "policy execution kind differs from compiler/state")
        require(row["id"] == coverage_tools._coverage_row_id(identity, row) and row.get("owner_shard") == coverage_tools._coverage_row_owner(row), "invalid policy row identity/owner")
    policy = coverage.get("policy", {})
    require(type(policy.get("version")) is int and policy["version"] == coverage_tools.COVERAGE_POLICY_VERSION,
            "coverage policy version differs from independent lane anchor")
    require(type(coverage.get("partition_version")) is int and coverage["partition_version"] == coverage_tools.COVERAGE_PARTITION_VERSION,
            "coverage partition version differs from independent lane contract")
    anchor = coverage_tools._COVERAGE_POLICY_ANCHORS.get((identity["platform"], identity["architecture"]))
    require(anchor is not None, "coverage platform has no independent lane anchor")
    fields = ("row_count", "required_count", "excluded_count", "fingerprint")
    require(all(type(policy.get(k)) is int for k in fields[:3]) and tuple(policy.get(k) for k in fields) == anchor,
            "coverage policy census/fingerprint differs from independent lane anchor")
    require(policy["row_count"] == len(rows) and policy["required_count"] == sum(r["state"] == "required" for r in expected) and
            policy["excluded_count"] == sum(r["state"] == "excluded" for r in expected) and policy["fingerprint"] == coverage_tools._coverage_policy_fingerprint(identity, expected), "policy census/fingerprint mismatch")
    selected = coverage_tools._coverage_selected_ids(rows, identity["shard"])
    has_unity = any(rows[row_id].get("unity") for row_id in selected)
    obligations = coverage_tools._coverage_expected_obligations(identity, "ci", environment, has_unity)
    require(coverage.get("obligations") == {name: {"state": state, "reason": reason} for name, (state, reason) in obligations.items()},
            "coverage obligations differ from independent lane policy")
    return rows, selected


def observation(root, item, test, unit, manifest_path, event, identity):
    path = retained(root, test["observation"])
    expected = (root / item["phase_directory"]).parent / "unit-observations" / event["id"] / "observation.json"
    require(path.resolve() == expected.resolve(), "observation is not the exact native task sidecar")
    value = phases.read(path)
    require(value.get("schema") == "buster-desktop-unit-observation-v1", "unknown native unit observation schema")
    require(all(value.get(k) == event[k] for k in ("id", "epoch_us", "pid", "argv")), "unit observation phase identity/command mismatch")
    require(all(value.get(k) == identity[k] for k in ("source_revision", "run_id", "run_attempt")), "unit observation source/run mismatch")
    require(type(value.get("test_result")) is int and value["test_result"] == 0 and value.get("binary_unchanged") is True and value.get("capture_complete") is True,
            "native unit observation failed, incomplete or binary changed")
    binary = event["argv"][2] if len(event["argv"]) > 2 and event["argv"][1] == "test_units_partitioned" else event["argv"][0]
    source_path = identity.get("source_path")
    paths = (ntpath if identity["platform"] == "windows" else posixpath) if source_path else os.path
    source_directory = paths.dirname(source_path) if source_path else os.getcwd()
    require(isinstance(value.get("binary_path"), str) and bool(value["binary_path"]), "missing observed native binary path")
    observed_path = paths.normcase(paths.normpath(paths.join(source_directory, value["binary_path"])))
    expected_path = paths.normcase(paths.normpath(paths.join(source_directory, binary)))
    require(observed_path == expected_path and value.get("binary_sha256") == unit["identity"]["binary_sha256"], "native test binary/path mismatch")
    require(value.get("inventory_file") == "inventory.log" and value.get("log_file") == "test.log", "native observation sidecar filenames changed")
    inventory_path = retained(path.parent, {"path": "inventory.log", "sha256": value.get("inventory_sha256")})
    log_path = retained(path.parent, {"path": "test.log", "sha256": value.get("log_sha256")})
    manifest = phases.read(manifest_path)
    require((manifest_path.parent / manifest["log"]).resolve() == log_path.resolve() and test["log_sha256"] == value["log_sha256"], "manifest did not consume the native test log")
    require(unit["inventory"] == unit_campaign.inventory(inventory_path), "native independent inventory differs from test manifest")
    return value


def desktop(root, item, run, condition, variant):
    coverage, result, summary = (record(root, item[k]) for k in ("coverage", "result", "phases"))
    require(result.get("success") is True and not result.get("coverage_errors") and not result.get("unsatisfied_steps"), "desktop result has incomplete coverage/steps")
    require(coverage.get("kind") == "desktop-matrix-coverage" and coverage.get("mode") == "ci" and coverage.get("phase") == "complete", "incomplete coverage manifest")
    meta = result.get("metadata", {})
    identity = coverage.get("identity", {})
    require(str(identity.get("run_id")) == str(run["id"]) and identity.get("run_attempt") == "1" and identity.get("source_revision") == run["head_sha"], "desktop coverage belongs to a different run/source")
    require(meta.get("GITHUB_SHA") == run["head_sha"] and str(meta.get("GITHUB_RUN_ID")) == str(run["id"]) and meta.get("GITHUB_RUN_ATTEMPT") == "1", "desktop result run identity mismatch")
    environment = phase_environment(summary, meta)
    rows, selected = policy_rows(coverage, environment)
    report = phases.analyze(root / item["phase_directory"], coverage, environment)
    require(report == summary and result.get("matrix_phases") == summary, "retained phase summary differs from native journal replay/result")
    require(COMMIT.fullmatch(report["identity"]["source_tree"]) and all(HASH.fullmatch(str(identity.get(k, ""))) for k in ("source_hash", "driver_hash")), "missing exact source/tree/driver identity")
    shard = item["job"].rsplit(" ", 1)[1]
    require(identity.get("shard") == shard, "desktop job/shard mismatch")
    expected_platform = item["job"].split(" ", 1)[0].lower()
    expected_arch = "x86_64" if "x86-64" in item["job"] else "aarch64"
    require(identity.get("platform") == expected_platform and identity.get("architecture") == expected_arch and
            identity.get("repository") == "buster14a/buster", "desktop job/platform/repository mismatch")
    admission = "all-builds" if variant == "combined-all-builds" and item["job"] == "Windows x86-64 checks" else "overlap"
    require(report["test_admission"] == admission, "native test admission differs from variant")
    runner = report["runner"]
    for field, key in (("ImageOS", "image_os"), ("ImageVersion", "image_version"), ("BUSTER_CI_RUNNER", "runner")):
        require(known(runner.get(field)) and runner[field] == condition[key], "phase/conditions runner image mismatch")
    require(known(runner.get("BUSTER_CI_ZIG_CACHE_HIT")) and condition["caches"].get("BUSTER_CI_ZIG_CACHE_HIT") == runner["BUSTER_CI_ZIG_CACHE_HIT"], "missing/mismatched actual Zig cache condition")
    expected, policy = coverage["expected"], coverage["policy"]
    executed = coverage.get("executed", [])
    require(len(executed) == 1 and executed[0].get("status") == "success" and executed[0].get("evidence") == "driver-complete" and
            executed[0].get("lane_id") == identity["lane_id"] and Counter(executed[0].get("rows", [])) == Counter(selected), "incomplete/duplicate selected-row completion")
    capabilities = {row["id"]: row for row in coverage.get("detected", [])}
    require(set(capabilities) == set(rows), "missing full capability census")
    runtime = {key for key in selected if rows[key].get("execution") == "runtime"}
    tests = item.get("tests", [])
    require(Counter(t["row_id"] for t in tests) == Counter(runtime), "missing/duplicate runtime assertion census")
    census = {}
    for test in tests:
        row, cap = rows[test["row_id"]], capabilities[test["row_id"]]
        manifest_path = retained(root, test["manifest"])
        manifest = phases.read(manifest_path)
        retained(manifest_path.parent, {"path": manifest["log"], "sha256": test["log_sha256"]})
        unit = units.validate_sample(manifest_path)
        provenance = unit["identity"]
        require(provenance["source_revision"] == run["head_sha"] and all(provenance[k] == identity[k] for k in ("platform", "architecture")) and
                all(provenance[k] == row[k] for k in ("configuration", "sanitize", "fuzz")) and provenance["cpu_budget"] == report["cpu_budget"], "test source/configuration/budget mismatch")
        require(provenance["table_audits"] == (row["unity"] if report["scheduler"] == "pooled" else True), "runtime table audit policy differs from source-bound row")
        require(provenance["toolchain"] == {k: cap[k] for k in CAP_KEYS} and provenance["runner_image"] == {k: condition[k] for k in ("image_os", "image_version", "runner")}, "test toolchain/image mismatch")
        tree = next(t for t in report["trees"] if test["row_id"] in t["rows"])
        event = next(e for e in report["events"] if e["id"] == phases.task_id(tree["id"], "test", row["configuration"]))
        require(unit["wall_us"] == event["end_us"] - event["child_start_us"] and unit["test_workers"] == int(event["test_jobs"]), "test census is not the native invocation interval/quota")
        observation(root, item, test, unit, manifest_path, event, identity)
        census[test["row_id"]] = {"inventory": unit["inventory"], "skipped_table_audits": unit["skipped_table_audits"],
            "external": unit["external"], "modules": {name: {k: module[k] for k in ("index", "assertions", "passed", "failed", "status")} for name, module in unit["modules"].items()}}
    caps = [{k: cap.get(k) for k in ("id", *CAP_KEYS, "state", "reason")} for cap in capabilities.values()]
    normalized_rows = [{k: v for k, v in row.items() if k != "owner_shard"} for row in expected]
    return {"platform": item["job"].rsplit(" ", 1)[0], "source_tree": report["identity"]["source_tree"], "source_hash": identity["source_hash"],
            "policy": policy, "rows": normalized_rows, "capabilities": caps,
            "logical_cpus": report["logical_cpus"], "cpu_budget": report["cpu_budget"], "selected": selected, "census": census}


def sample(root, item):
    variant = item["variant"]
    require(variant in VARIANTS, "unknown qualification variant")
    run = record(root, item["run"])
    measured = timing(run, variant)
    entries, normalized = conditions(root, item["conditions"], run)
    desktop_names = github.SPLIT_COMBINATION_PLATFORMS if variant == "split-overlap" else github.COMBINATION_PLATFORMS
    items = item.get("desktops", [])
    require(Counter(i["job"] for i in items) == Counter(desktop_names), "missing/duplicate desktop evidence")
    platforms = {}
    for evidence in items:
        value = desktop(root, evidence, run, entries[evidence["job"]], variant)
        key = value.pop("platform")
        selected, census = value.pop("selected"), value.pop("census")
        if key not in platforms:
            platforms[key] = {"identity": value, "selected": set(), "census": {}}
        target = platforms[key]
        require(target["identity"] == value, "platform source/policy/toolchain/quota differs between jobs")
        require(not target["selected"].intersection(selected), "duplicate policy row across platform jobs")
        target["selected"].update(selected)
        target["census"].update(census)
    for value in platforms.values():
        require(value["selected"] == {r["id"] for r in value["identity"]["rows"] if r["state"] == "required"}, "split union omitted original required rows")
        value["selected"] = sorted(value["selected"])
    return {"variant": variant, "run_id": run["id"], "head_sha": run["head_sha"], "workflow_blob_sha": run["workflow_blob_sha"],
            "conditions": normalized, "platforms": platforms, "timing": measured}


def qualify(path):
    path = Path(path)
    output = {"schema": SCHEMA, "status": "pending", "performance_accepted": False,
              "timing_status": "pending", "timing_contract_met": False,
              "resource_review": "pending", "errors": [], "samples": [], "issues": {}}
    try:
        campaign = phases.read(path)
        require(campaign.get("schema") == SCHEMA and campaign.get("repository") == "buster14a/buster", "unknown campaign schema/repository")
        items = campaign.get("samples", [])
        require(Counter(i.get("variant") for i in items) == Counter({v: 3 for v in VARIANTS}), "need exactly three complete first attempts per variant")
        observations = [sample(path.parent, item) for item in items]
        require(len({o["run_id"] for o in observations}) == len(observations), "a GitHub run was counted more than once")
        reference = observations[0]
        for observation in observations:
            for key in ("head_sha", "workflow_blob_sha", "conditions", "platforms"):
                require(observation[key] == reference[key], "incomparable campaign: " + key)
        groups = defaultdict(list)
        for observation in observations:
            groups[observation["variant"]].append(observation["timing"])
        medians = {v: {k: statistics.median(t[k] for t in values) for k in ("elapsed_seconds", "runner_seconds")} for v, values in groups.items()}
        for variant in VARIANTS[:2]:
            medians[variant]["windows_checks_seconds"] = statistics.median(t["job_seconds"]["Windows x86-64 checks"] for t in groups[variant])
        baseline = medians["combined-overlap"]
        for issue, variant, metric, ratio in (("2119", "combined-all-builds", "windows_checks_seconds", .90), ("2120", "split-overlap", "elapsed_seconds", .85)):
            candidate = medians[variant]
            wall_ratio, runner_ratio = candidate[metric] / baseline[metric], candidate["runner_seconds"] / baseline["runner_seconds"]
            accepted = wall_ratio <= ratio and runner_ratio <= 1.05
            output["issues"][issue] = {"status": "pending" if accepted else "rejected",
                                       "timing_status": "accepted" if accepted else "rejected", "variant": variant, "metric": metric,
                                       "time_ratio": wall_ratio, "maximum_time_ratio": ratio, "runner_seconds_ratio": runner_ratio, "maximum_runner_seconds_ratio": 1.05}
        timing_met = all(i["timing_status"] == "accepted" for i in output["issues"].values())
        output.update(status="pending" if timing_met else "rejected",
                      timing_status="accepted" if timing_met else "rejected", timing_contract_met=timing_met,
                      pending_reviews=["CPU time and peak RSS observations", "resource/deadline/cleanup/reliability comparison"], medians=medians,
                      head_sha=reference["head_sha"], workflow_blob_sha=reference["workflow_blob_sha"],
                      samples=[{k: o[k] for k in ("variant", "run_id", "timing")} for o in observations])
    except (OSError, ValueError, KeyError, TypeError, AttributeError, StopIteration) as error:
        output["errors"].append(str(error))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    report = qualify(args.campaign)
    print(json.dumps(report, indent=2) + "\n", end="")
    return {"rejected": 1, "pending": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
