#!/usr/bin/env python3
"""Run the fixed native-retirement performance experiment.

The service passes only two trusted compiler binaries, their immutable source
identities, and a private empty output directory.  All workload commands,
support evidence, host receipts, sampling policy, and publication identity come
from one operator-installed definition whose top-level bytes are pinned by the
calling build driver.  Result records are written in canonical row/round/pair
order and rotated below the #615 record and byte limits.
"""

import argparse
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import resource
import re
import shutil
import signal
import sqlite3
import stat
import tarfile
import tempfile
import time


SCHEMA = "buster-native-retirement-experiment-definition-v1"
PLAN_SCHEMA = "buster-native-retirement-measurement-plan-v1"
VERDICT_SCHEMA = "buster-native-retirement-performance-verdict-v1"
HEX = frozenset("0123456789abcdef")
MAX_FILE_BYTES = 2 * 1024 * 1024 * 1024
MAX_TOTAL_BYTES = 16 * 1024 * 1024 * 1024
READ_BYTES = 64 * 1024
ENVIRONMENT_NAME = re.compile(r"[A-Z_][A-Z0-9_]*\Z")


class ExperimentError(RuntimeError):
    """A fail-closed experiment or evidence error."""


def fail(message):
    raise ExperimentError(message)


def exact(value, fields, name):
    if not isinstance(value, dict) or set(value) != set(fields):
        fail(f"{name} fields differ")
    return value


def token(value, name):
    if (not isinstance(value, str) or not value or len(value) > 128
            or any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-" for character in value)):
        fail(f"{name} is not a bounded token")
    return value


def digest_text(value, name):
    if not isinstance(value, str) or len(value) != 64 or any(character not in HEX for character in value):
        fail(f"{name} is not a lowercase SHA-256")
    return value


def commit(value, name):
    if not isinstance(value, str) or len(value) != 40 or any(character not in HEX for character in value):
        fail(f"{name} is not a lowercase commit identity")
    return value


def relative(value, name):
    if not isinstance(value, str):
        fail(f"{name} is not a relative path")
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or value != path.as_posix()
            or any(part in ("", ".", "..") for part in path.parts)
            or len(value.encode("utf-8")) > 4096):
        fail(f"{name} is not a canonical relative path")
    return value


def canonical_root(value, name, writable=False):
    path = Path(value)
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        fail(f"{name} cannot be resolved: {error}")
    if (not path.is_absolute() or str(path) != os.path.normpath(str(path))
            or path != resolved or path.is_symlink()):
        fail(f"{name} is not a canonical absolute directory")
    info = path.stat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid not in (0, os.geteuid())
            or (not writable and info.st_mode & 0o222)):
        fail(f"{name} directory ownership or mode is not trusted")
    return path


def sha256_file(path, maximum=MAX_FILE_BYTES):
    info = path.stat(follow_symlinks=False)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size < 0 or info.st_size > maximum:
        fail(f"untrusted or oversized regular file: {path}")
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(READ_BYTES), b""):
            count += len(chunk)
            if count > maximum:
                fail(f"file grew beyond its bound: {path}")
            digest.update(chunk)
    after = path.stat(follow_symlinks=False)
    if (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) != \
            (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns):
        fail(f"file changed while hashing: {path}")
    return count, digest.hexdigest()


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def descriptor(path, data):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def write_new(root, path, data):
    relative(path, "output path")
    target = root.joinpath(*PurePosixPath(path).parts)
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor_fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o400)
    try:
        offset = 0
        while offset < len(data):
            count = os.write(descriptor_fd, data[offset:])
            if count <= 0:
                fail(f"cannot write evidence file {path}")
            offset += count
        os.fsync(descriptor_fd)
    finally:
        os.close(descriptor_fd)
    return descriptor(path, data)


def copy_new(root, path, source, expected):
    relative(path, "output path")
    target = root.joinpath(*PurePosixPath(path).parts)
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    descriptor_fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o400)
    digest = hashlib.sha256()
    count = 0
    try:
        with source.open("rb") as stream:
            for chunk in iter(lambda: stream.read(READ_BYTES), b""):
                count += len(chunk)
                if count > expected["bytes"] or count > MAX_FILE_BYTES:
                    fail(f"copied evidence member exceeds its bound: {path}")
                digest.update(chunk)
                offset = 0
                while offset < len(chunk):
                    written = os.write(descriptor_fd, chunk[offset:])
                    if written <= 0:
                        fail(f"cannot copy evidence member: {path}")
                    offset += written
        os.fsync(descriptor_fd)
    finally:
        os.close(descriptor_fd)
    copied = {"path": path, "bytes": count, "sha256": digest.hexdigest()}
    if copied != expected:
        fail(f"copied evidence member differs: {path}")
    return copied


def load_definition(root, expected_sha256):
    manifest = root / "definition.json"
    manifest_info = manifest.stat(follow_symlinks=False)
    if manifest_info.st_mode & 0o222 or manifest_info.st_uid not in (0, os.geteuid()):
        fail("experiment definition manifest is writable or has an untrusted owner")
    size, actual = sha256_file(manifest, 16 * 1024 * 1024)
    if actual != expected_sha256:
        fail("experiment definition digest differs from the compiled service identity")
    try:
        value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"experiment definition is unreadable: {error}")
    value = exact(value, ("schema", "version", "decision_id", "binding_template", "measurement_plan",
                          "validator", "files", "publication"), "definition")
    if value["schema"] != SCHEMA or value["version"] != 1 or value["decision_id"] != "native-retirement-performance-v1":
        fail("experiment definition schema/version/decision is not approved")
    files = value["files"]
    if not isinstance(files, list) or not files:
        fail("experiment definition has no immutable files")
    total = size
    paths = set()
    for index, item in enumerate(files):
        item = exact(item, ("path", "bytes", "sha256"), f"definition.files[{index}]")
        path = relative(item["path"], f"definition.files[{index}].path")
        if path in paths or type(item["bytes"]) is not int or not 0 <= item["bytes"] <= MAX_FILE_BYTES:
            fail("experiment definition contains a duplicate or invalid file")
        digest_text(item["sha256"], f"definition.files[{index}].sha256")
        target = root.joinpath(*PurePosixPath(path).parts)
        target_info = target.stat(follow_symlinks=False)
        if target_info.st_mode & 0o222 or target_info.st_uid not in (0, os.geteuid()):
            fail(f"experiment definition member is writable or has an untrusted owner: {path}")
        count, actual = sha256_file(target)
        if count != item["bytes"] or actual != item["sha256"]:
            fail(f"experiment definition member differs: {path}")
        total += count
        if total > MAX_TOTAL_BYTES:
            fail("experiment definition exceeds the immutable total-byte cap")
        paths.add(path)
    for name in ("binding_template", "measurement_plan", "validator"):
        item = exact(value[name], ("path", "bytes", "sha256"), f"definition.{name}")
        if item["path"] not in paths:
            fail(f"definition.{name} is not an authenticated member")
        matched = next(candidate for candidate in files if candidate["path"] == item["path"])
        if item != matched:
            fail(f"definition.{name} descriptor differs from its member")
    publication = exact(value["publication"], ("publisher", "release", "run_id", "publication_id"),
                        "definition.publication")
    for name, item in publication.items():
        token(item, f"definition.publication.{name}")
    return value


def copy_static(definition_root, output, definition):
    excluded = {definition["binding_template"]["path"], definition["measurement_plan"]["path"]}
    for item in definition["files"]:
        if item["path"] not in excluded:
            source = definition_root.joinpath(*PurePosixPath(item["path"]).parts)
            copy_new(output, item["path"], source, item)


def import_validator(definition_root, definition):
    path = definition_root.joinpath(*PurePosixPath(definition["validator"]["path"]).parts)
    spec = importlib.util.spec_from_file_location("native_retirement_performance_binding", path)
    if spec is None or spec.loader is None:
        fail("cannot load the authenticated binding validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expand(arguments, compiler, source, work, artifact):
    replacements = {"{compiler}": str(compiler), "{source}": str(source),
                    "{work}": str(work), "{artifact}": str(artifact)}
    result = []
    for value in arguments:
        if not isinstance(value, str) or "\0" in value or "\n" in value or "\r" in value:
            fail("measurement argv contains an invalid value")
        for marker, replacement in replacements.items():
            value = value.replace(marker, replacement)
        if "{" in value or "}" in value:
            fail("measurement argv contains an unknown placeholder")
        result.append(value)
    if not result or not Path(result[0]).is_absolute():
        fail("measurement commands must use an absolute executable")
    return result


def run_process(arguments, cwd, environment, timeout, output):
    started = time.monotonic_ns()
    child = os.fork()
    if child == 0:
        try:
            os.setsid()
            descriptor_fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
            null_fd = os.open("/dev/null", os.O_RDONLY | os.O_CLOEXEC)
            os.dup2(null_fd, 0)
            os.dup2(descriptor_fd, 1)
            os.dup2(descriptor_fd, 2)
            os.chdir(cwd)
            os.execve(arguments[0], arguments, environment)
        except BaseException:
            os._exit(127)
    deadline = time.monotonic() + timeout
    status = None
    usage = None
    while status is None and time.monotonic() < deadline:
        waited, candidate, candidate_usage = os.wait4(child, os.WNOHANG)
        if waited == child:
            status, usage = candidate, candidate_usage
        else:
            time.sleep(0.01)
    if status is None:
        try:
            os.killpg(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        _waited, status, usage = os.wait4(child, 0)
        fail(f"measurement command timed out: {arguments[0]}")
    elapsed = time.monotonic_ns() - started
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        fail(f"measurement command failed: {arguments[0]}")
    return max(elapsed, 1), max(int(usage.ru_maxrss), 1)


def measured_side(row, compiler, source, root, label, environment, timeout):
    work = root / label
    work.mkdir(parents=True, mode=0o700)
    artifact = work / row["artifact"]
    artifact.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    log = work / "compile.log"
    cwd = (source / row["cwd"]).resolve()
    try:
        cwd.relative_to(source)
    except ValueError:
        fail("measurement working directory escapes its immutable source")
    arguments = expand(row["compile_argv"], compiler, source, work, artifact)
    wall, rss = run_process(arguments, cwd, environment, timeout, log)
    if not artifact.is_file() or artifact.is_symlink():
        fail("compiler did not produce the planned artifact")
    artifact_bytes, artifact_digest = sha256_file(artifact)
    size_output = work / "code-size.txt"
    size_arguments = expand(row["code_size_argv"], compiler, source, work, artifact)
    run_process(size_arguments, cwd, environment, timeout, size_output)
    try:
        code_bytes = int(size_output.read_text(encoding="ascii").strip())
    except (OSError, UnicodeError, ValueError):
        fail("code-size extractor did not emit one bounded integer")
    if not 0 < code_bytes <= (1 << 63) - 1:
        fail("code-size extractor result is outside the bounded domain")
    runtime = None
    runtime_digest = None
    if row["runtime_argv"]:
        runtime_output = work / "runtime.out"
        runtime_arguments = expand(row["runtime_argv"], compiler, source, work, artifact)
        runtime, _runtime_rss = run_process(runtime_arguments, cwd, environment,
                                             timeout, runtime_output)
        _runtime_bytes, runtime_digest = sha256_file(runtime_output)
    return {"compiler_wall_time": wall, "compiler_peak_rss": rss,
            "generated_code_bytes": code_bytes, "generated_runtime": runtime,
            "artifact_bytes": artifact_bytes, "artifact_sha256": artifact_digest,
            "runtime_sha256": runtime_digest}


def paired_measurement(row, left, right, roots, label, environment, timeout, reverse):
    order = (("candidate", right), ("baseline", left)) if reverse else (("baseline", left), ("candidate", right))
    results = {}
    for side, (compiler, source) in order:
        results[side] = measured_side(row, compiler, source, roots / side,
                                      f"{label}-{side}", environment, timeout)
    if results["baseline"]["runtime_sha256"] != results["candidate"]["runtime_sha256"]:
        fail("native runtime oracle output differs between paired subjects")
    return results


def measurement_record(row, round_number, pair, measured):
    values = {}
    for metric in row["metrics"]:
        baseline = measured["baseline"][metric]
        candidate = measured["candidate"][metric]
        if baseline is None or candidate is None:
            fail("planned metric has no native observation")
        values[metric] = {"baseline": baseline, "candidate": candidate}
    return {"record_id": f"row-{row['row']}/round-{round_number}/pair-{pair}",
            "row": row["row"], "round": round_number, "pair": pair,
            "measurements": values}


def outcome(adapter):
    outcomes = [item.get("outcome") for item in adapter.get("members", [])]
    if not outcomes or any(item == "invalid" for item in outcomes):
        return "invalid"
    if any(item == "regression" for item in outcomes):
        return "regression"
    if any(item == "inconclusive" for item in outcomes):
        return "inconclusive"
    if any(item != "pass" for item in outcomes):
        return "invalid"
    return "pass"


def run(arguments):
    definition_root = canonical_root(arguments.definition, "definition")
    output = canonical_root(arguments.output, "output", writable=True)
    if any(output.iterdir()):
        fail("performance output directory must be empty")
    definition = load_definition(definition_root, digest_text(arguments.definition_sha256, "definition digest"))
    copy_static(definition_root, output, definition)
    binding = import_validator(definition_root, definition)
    template_path = definition_root.joinpath(*PurePosixPath(definition["binding_template"]["path"]).parts)
    plan_path = definition_root.joinpath(*PurePosixPath(definition["measurement_plan"]["path"]).parts)
    record = json.loads(template_path.read_text(encoding="utf-8"))
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    plan = exact(plan, ("schema", "version", "rows", "environment", "timeout_seconds", "aa_limits"), "measurement plan")
    if plan["schema"] != PLAN_SCHEMA or plan["version"] != 1:
        fail("measurement plan schema/version is not approved")
    baseline_id = commit(arguments.baseline_id, "baseline id")
    candidate_id = commit(arguments.candidate_id, "candidate id")
    if (record["subjects"]["baseline"]["source_commit"] != baseline_id
            or record["subjects"]["candidate"]["source_commit"] != candidate_id):
        fail("request subjects differ from the compiled experiment definition")
    baseline_binary = Path(arguments.baseline)
    candidate_binary = Path(arguments.candidate)
    baseline_source = canonical_root(arguments.baseline_source, "baseline source")
    candidate_source = canonical_root(arguments.candidate_source, "candidate source")
    for name, binary in (("baseline", baseline_binary), ("candidate", candidate_binary)):
        _size, actual = sha256_file(binary)
        if actual != record["subjects"][name]["binary"]["sha256"]:
            fail(f"{name} executable differs from its frozen definition")
        target_path = record["subjects"][name]["binary"]["path"]
        target = output.joinpath(*PurePosixPath(target_path).parts)
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        copy_new(output, target_path, binary, record["subjects"][name]["binary"])
    rows_descriptor = record["support"]["files"][binding.SUPPORT_FILE_ROLES.index("performance_rows")]
    performance_rows = json.loads(output.joinpath(*PurePosixPath(rows_descriptor["path"]).parts).read_text(encoding="utf-8"))
    parsed, _axes, family = binding._performance_rows(json_bytes(performance_rows))
    rows = plan["rows"]
    if not isinstance(rows, list) or [item.get("row") for item in rows] != [item["row"] for item in parsed]:
        fail("measurement plan does not cover the canonical performance rows in order")
    parsed_by_id = {item["row"]: item for item in parsed}
    allowed_metrics = set(binding.METRICS)
    if set(plan["aa_limits"]) != allowed_metrics:
        fail("A/A policy does not cover every approved metric")
    for metric in allowed_metrics:
        limit = plan["aa_limits"][metric]
        if type(limit) not in (int, float) or float(limit) != float(binding.AGGREGATE_THRESHOLDS[metric]):
            fail("A/A policy differs from the approved aggregate guard")
    for index, row in enumerate(rows):
        exact(row, ("row", "census_row", "cwd", "compile_argv", "artifact", "runtime_argv",
                    "code_size_argv", "metrics"), f"measurement_plan.rows[{index}]")
        relative(row["cwd"], f"measurement_plan.rows[{index}].cwd")
        relative(row["artifact"], f"measurement_plan.rows[{index}].artifact")
        expected_metrics = {name for name, enabled in parsed_by_id[row["row"]]["metrics"].items() if enabled}
        if not isinstance(row["metrics"], list) or set(row["metrics"]) != expected_metrics or not set(row["metrics"]) <= allowed_metrics:
            fail("measurement plan metric set differs from canonical eligibility")
        if bool(row["runtime_argv"]) != ("generated_runtime" in expected_metrics):
            fail("measurement plan runtime command differs from canonical eligibility")
    environment = {"LC_ALL": "C", "LANG": "C", "TZ": "UTC"}
    if not isinstance(plan["environment"], dict):
        fail("measurement environment is not an object")
    for name, value in plan["environment"].items():
        if not isinstance(name, str) or ENVIRONMENT_NAME.fullmatch(name) is None:
            fail("measurement environment name is invalid")
        if not isinstance(value, str) or "\0" in value:
            fail("measurement environment value is invalid")
        environment[name] = value
    timeout = plan["timeout_seconds"]
    if type(timeout) is not int or not 1 <= timeout <= 3600:
        fail("measurement timeout is outside the fixed bound")
    rounds = record["rules"]["sampling"]["rounds"]
    pairs = record["rules"]["sampling"]["pairs_per_round"]
    warmups = record["rules"]["sampling"]["warmups_per_variant"]
    if rounds != arguments.rounds or pairs < arguments.minimum_pairs_per_round or pairs > arguments.maximum_pairs_per_round \
            or pairs % 2 or warmups != arguments.warmups_per_variant:
        fail("fixed command policy differs from the frozen binding")
    required_records = len(rows) * rounds * pairs
    if required_records > arguments.total_record_cap:
        fail("experiment exceeds its immutable record ceiling")
    partition_cap = arguments.partition_record_cap
    partitions = (required_records + partition_cap - 1) // partition_cap
    if partitions < 1 or partitions > arguments.partition_count_cap:
        fail("experiment partition count exceeds its immutable bound")

    plan_manifests = []
    for index in range(partitions):
        start = index * partition_cap
        plan_manifests.append({"identity": f"result-input-manifest-{index:03d}",
                               "path": f"results/input-manifest-{index:03d}.json",
                               "start_record": start,
                               "records": min(partition_cap, required_records - start)})
    support_files = record["support"]["files"]
    performance_declaration = json.loads(output.joinpath(*PurePosixPath(
        support_files[binding.SUPPORT_FILE_ROLES.index("performance_declaration")]["path"]).parts).read_text(encoding="utf-8"))
    input_plan = {
        "schema": binding.RESULT_INPUT_PLAN_SCHEMA, "version": 1,
        "source_manifest_sha256": support_files[binding.SUPPORT_FILE_ROLES.index("manifest")]["sha256"],
        "source_rows_sha256": support_files[binding.SUPPORT_FILE_ROLES.index("rows")]["sha256"],
        "object_row_count": performance_declaration["object_row_count"],
        "sample_row_count": len(rows), "rounds": rounds, "identity_field": "record_id",
        "coordinate_schema": "row-round-pair-v1",
        "sample_population": "canonical-performance-rows-with-required-metrics",
        "eligible_population": "canonical-performance-rows", "pairs_per_round": pairs,
        "records_per_row": rounds * pairs, "required_records": required_records,
        "max_records_per_manifest": binding.RESULT_INPUT_MAX_RECORDS,
        "manifest_count": partitions, "manifests": plan_manifests, "predeclared": True,
    }
    plan_descriptor = write_new(output, "results/input-plan.json", json_bytes(input_plan))
    record["workflow"]["records"]["result_input_plan"] = plan_descriptor
    counts = binding._family_member_counts(family)
    pre_value = {
        "schema": binding.PHASE_SCHEMA["pre_sample_plan"], "version": 1,
        "status": "frozen-before-samples", "support_declaration_sha256": support_files[0]["sha256"],
        "manifest_sha256": support_files[2]["sha256"], "rows_sha256": support_files[4]["sha256"],
        "family_sha256": family["sha256"], "seed": record["rules"]["sampling"]["seed"],
        "rounds": rounds, "pairs_per_round": pairs, "resamples": record["rules"]["sampling"]["resamples"],
        "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
        "cell_members_per_scope": counts["cell_members_per_scope"],
        "result_input_plan_sha256": plan_descriptor["sha256"],
    }
    pre_descriptor = write_new(output, "workflow/pre-sample-plan.json", json_bytes(pre_value))
    record["workflow"]["phases"]["pre_sample_plan"] = pre_descriptor

    work = output / ".measurement-work"
    work.mkdir(mode=0o700)
    aa_ok = True
    aa_observation_count = 0
    aa_path = output / "execution/aa-observations.jsonl"
    aa_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    aa_digest = hashlib.sha256()
    aa_bytes = 0
    with aa_path.open("xb") as aa_stream:
        for row in rows:
            for warmup in range(warmups):
                warmup_root = work / f"aa-warmup-{row['row']}-{warmup}"
                warmup_root.mkdir(mode=0o700)
                paired_measurement(row, (baseline_binary, baseline_source), (baseline_binary, baseline_source),
                                   warmup_root, "warmup", environment, timeout, bool(warmup & 1))
                shutil.rmtree(warmup_root)
            for round_number in range(rounds):
                for pair in range(pairs):
                    pair_root = work / f"aa-{row['row']}-{round_number}-{pair}"
                    pair_root.mkdir(mode=0o700)
                    measured = paired_measurement(row, (baseline_binary, baseline_source),
                                                   (baseline_binary, baseline_source), pair_root, "pair",
                                                   environment, timeout, bool((pair // 2 + round_number) & 1))
                    measurements = {}
                    for metric in row["metrics"]:
                        baseline_value = measured["baseline"][metric]
                        candidate_value = measured["candidate"][metric]
                        ratio = candidate_value / baseline_value
                        limit = plan["aa_limits"].get(metric)
                        if type(limit) not in (int, float) or not math.isfinite(limit) or limit < 1:
                            fail("A/A limit is invalid")
                        aa_ok = aa_ok and 1.0 / float(limit) <= ratio <= float(limit)
                        measurements[metric] = {"baseline": baseline_value, "candidate": candidate_value}
                    observation = json_bytes({
                        "record_id": f"aa-row-{row['row']}/round-{round_number}/pair-{pair}",
                        "row": row["row"], "round": round_number, "pair": pair,
                        "measurements": measurements,
                    })
                    aa_stream.write(observation)
                    aa_digest.update(observation)
                    aa_bytes += len(observation)
                    if aa_bytes > arguments.total_byte_cap:
                        fail("A/A evidence exceeds the immutable total-byte cap")
                    aa_observation_count += 1
                    shutil.rmtree(pair_root)
        aa_stream.flush()
        os.fsync(aa_stream.fileno())
    os.chmod(aa_path, 0o400)
    if not aa_observation_count or not aa_ok:
        fail("same-binary A/A admission failed")
    host = record["execution"]["host"]
    profile = record["execution"]["profile"]
    admission_value = {
        "schema": binding.AA_SCHEMA, "version": 1, "machine_id": host["machine_id"],
        "profile_id": profile["id"], "profile_version": profile["version"],
        "service_id": record["execution"]["service"]["id"], "admitted": True, "native_only": True,
        "baseline_source_commit": record["subjects"]["baseline"]["source_commit"],
        "baseline_source_tree": record["subjects"]["baseline"]["source_tree"],
        "lease_protocol": binding.LEASE_PROTOCOL,
        "observations": {"path": "execution/aa-observations.jsonl", "bytes": aa_bytes,
                         "sha256": aa_digest.hexdigest()},
        "observation_count": aa_observation_count,
        "policy_sha256": binding._canonical_json_digest(plan["aa_limits"]),
    }
    admission_descriptor = write_new(output, "execution/aa-admission.json", json_bytes(admission_value))
    host["aa_admission_receipt"] = admission_descriptor
    post_value = dict(pre_value)
    post_value.update({"schema": binding.PHASE_SCHEMA["post_aa_binding"],
                       "status": "bound-after-aa-before-samples",
                       "pre_sample_plan_sha256": pre_descriptor["sha256"],
                       "aa_admission_sha256": admission_descriptor["sha256"]})
    post_descriptor = write_new(output, "workflow/post-aa-binding.json", json_bytes(post_value))
    record["workflow"]["phases"]["post_aa_binding"] = post_descriptor

    database = sqlite3.connect(work / "samples.sqlite3")
    database.execute("CREATE TABLE samples(row_id INTEGER, round_id INTEGER, pair_id INTEGER, metric TEXT, baseline TEXT, candidate TEXT)")
    manifests = []
    measurement_digest = hashlib.sha256()
    measurement_bytes = 0
    admission_records = []
    oracle_records = []
    global_record = 0
    partition_stream = None
    partition_digest = None
    partition_bytes = 0
    partition_records = 0
    partition_shards = []
    shard_index = 0

    def open_shard(partition):
        nonlocal partition_stream, partition_digest, partition_bytes, shard_index
        path = output / f"results/input-{partition:03d}-shard-{shard_index:03d}.jsonl"
        partition_stream = path.open("xb")
        partition_digest = hashlib.sha256()
        partition_bytes = 0
        shard_index += 1
        return path

    def close_shard(path):
        nonlocal partition_stream
        partition_stream.flush()
        os.fsync(partition_stream.fileno())
        partition_stream.close()
        relative_path = path.relative_to(output).as_posix()
        partition_shards.append({"identity": f"result-input-shard-{len(manifests):03d}-{len(partition_shards):03d}",
                                 "path": relative_path, "bytes": partition_bytes,
                                 "sha256": partition_digest.hexdigest()})
        partition_stream = None

    current_path = open_shard(0)
    first_results = {}
    for row in rows:
        for warmup in range(warmups):
            warmup_root = work / f"ab-warmup-{row['row']}-{warmup}"
            warmup_root.mkdir(mode=0o700)
            paired_measurement(row, (baseline_binary, baseline_source), (candidate_binary, candidate_source),
                               warmup_root, "warmup", environment, timeout, bool(warmup & 1))
            shutil.rmtree(warmup_root)
        for round_number in range(rounds):
            for pair in range(pairs):
                pair_root = work / f"ab-{row['row']}-{round_number}-{pair}"
                pair_root.mkdir(mode=0o700)
                measured = paired_measurement(row, (baseline_binary, baseline_source),
                                               (candidate_binary, candidate_source), pair_root, "pair",
                                               environment, timeout, bool((pair // 2 + round_number) & 1))
                first_results.setdefault(row["row"], measured["candidate"])
                value = measurement_record(row, round_number, pair, measured)
                data = json_bytes(value)
                if partition_bytes and partition_bytes + len(data) > MAX_FILE_BYTES:
                    close_shard(current_path)
                    current_path = open_shard(len(manifests))
                partition_stream.write(data)
                partition_digest.update(data)
                measurement_digest.update(data)
                partition_bytes += len(data)
                measurement_bytes += len(data)
                if measurement_bytes > arguments.total_byte_cap:
                    fail("streamed measurements exceed the immutable total-byte cap")
                partition_records += 1
                for metric, values in value["measurements"].items():
                    database.execute("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?)",
                                     (row["row"], round_number, pair, metric,
                                      str(values["baseline"]), str(values["candidate"])))
                global_record += 1
                shutil.rmtree(pair_root)
                expected_partition = plan_manifests[len(manifests)]["records"]
                if partition_records == expected_partition:
                    close_shard(current_path)
                    manifest_data = json_bytes({"schema": binding.RESULT_INPUT.MANIFEST_SCHEMA, "version": 1,
                                                "identity_field": "record_id", "shards": partition_shards})
                    manifest_descriptor = write_new(output, plan_manifests[len(manifests)]["path"], manifest_data)
                    manifests.append({"identity": plan_manifests[len(manifests)]["identity"], **manifest_descriptor,
                                      "start_record": global_record - partition_records,
                                      "records": partition_records,
                                      "input_bytes": len(manifest_data) + sum(item["bytes"] for item in partition_shards)})
                    partition_records = 0
                    partition_shards = []
                    shard_index = 0
                    if global_record < required_records:
                        current_path = open_shard(len(manifests))
    database.commit()
    if global_record != required_records or partition_stream is not None or len(manifests) != partitions:
        fail("measurement producer did not fill the frozen partition plan")

    for row in rows:
        observed = first_results[row["row"]]
        identity = parsed_by_id[row["row"]]["identity"]
        admission_records.append({
            "row": row["row"], "census_row": row["census_row"], "identity": identity,
            "artifact_stage": identity["artifact_stage"],
            "requested_obligation": "compiler-wall-time-and-peak-rss", "status": "completed",
            "exit_code": 0, "timed_out": False, "native_compiler": True,
            "artifact_kind": "object" if identity["artifact_stage"] == "object" else "linked-executable",
            "artifact_bytes": observed["artifact_bytes"], "artifact_sha256": observed["artifact_sha256"],
        })
        oracle_records.append({
            "row": row["row"], "code_section_status": "parsed-deterministic",
            "code_section_bytes": observed["generated_code_bytes"],
            "code_section_sha256": hashlib.sha256(str(observed["generated_code_bytes"]).encode("ascii")).hexdigest(),
            "runtime_oracle_status": "passed-native" if "generated_runtime" in row["metrics"] else "not-applicable",
            "runtime_exit_code": 0 if "generated_runtime" in row["metrics"] else -1,
            "native_runtime": "generated_runtime" in row["metrics"],
        })
    admission_record_descriptor = write_new(output, "execution/admission-records.json", json_bytes({
        "schema": binding.ADMISSION_SCHEMA, "version": 1,
        "source_manifest_sha256": support_files[2]["sha256"], "source_rows_sha256": support_files[4]["sha256"],
        "records": admission_records}))
    oracle_descriptor = write_new(output, "execution/oracle-records.json", json_bytes({
        "schema": binding.ORACLE_SCHEMA, "version": 1,
        "source_manifest_sha256": support_files[2]["sha256"], "source_rows_sha256": support_files[4]["sha256"],
        "records": oracle_records}))
    record["workflow"]["records"].update({"admission": admission_record_descriptor, "oracle": oracle_descriptor})

    bootstrap_index, cell_index = binding._family_member_indexes(family)
    series_path = output / "results/statistics-series.txt"
    with series_path.open("x", encoding="utf-8", newline="") as stream:
        stream.write(f"version=1 seed={record['rules']['sampling']['seed']} bootstrap_members={counts['bootstrap_members_per_scope']} "
                     f"cell_members={counts['cell_members_per_scope']} pairs={pairs} resamples={record['rules']['sampling']['resamples']} "
                     f"frozen=1 members={len(family['members'])}\n")
        for member in family["members"]:
            metric = member.split("/", 1)[0]
            is_cell = "/cell/" in member
            index = cell_index[member] if is_cell else bootstrap_index[member]
            limit = binding.CELL_THRESHOLDS[metric] if is_cell else binding.AGGREGATE_THRESHOLDS[metric]
            if is_cell:
                selected = [int(member.rsplit("=", 1)[1])]
            elif member.endswith("/aggregate"):
                selected = [item["row"] for item in parsed if item["metrics"].get(metric, False)]
            else:
                dimension, value = member.split("/slice/", 1)[1].split("=", 1)
                selected = [item["row"] for item in parsed if item["metrics"].get(metric, False)
                            and str(item["identity"][dimension]) == value]
            stream.write(f"member={member} metric={binding.STATISTICAL_METRICS.index(metric)} kind={1 if is_cell else 0} "
                         f"family={index} cells={len(selected)} pairs={pairs} "
                         f"resamples={0 if is_cell else record['rules']['sampling']['resamples']} limit={limit}\n")
            for row_id in selected:
                values = database.execute("SELECT baseline, candidate FROM samples WHERE row_id=? AND metric=? ORDER BY round_id, pair_id",
                                          (row_id, metric))
                for baseline_value, candidate_value in values:
                    stream.write(f"ratio={float(candidate_value) / float(baseline_value):.17g}\n")
            stream.write("end\n")
        stream.flush()
        os.fsync(stream.fileno())
    series_bytes, series_sha256 = sha256_file(series_path)
    adapter_input = {"path": "results/statistics-series.txt", "bytes": series_bytes, "sha256": series_sha256}
    adapter_directory = work / "adapter"
    adapter_directory.mkdir(mode=0o700)
    executable, _source_digest, source_closure_digest, toolchain_digest, build_command = \
        binding._compile_trusted_retirement_adapter(adapter_directory)
    adapter_output = output / "results/statistics-replay.json"
    process = os.spawnve(os.P_WAIT, str(executable), [str(executable), "retirement-replay", "--input", str(series_path),
                                                     "--output", str(adapter_output)], environment)
    if process != 0:
        fail("reviewed C statistics adapter failed")
    adapter_result_data = adapter_output.read_bytes()
    adapter_result = descriptor("results/statistics-replay.json", adapter_result_data)
    code_summary = binding._code_bytes_summary(parsed, database, rounds, pairs)
    database.close()
    result_bundle_value = {
        "schema": binding.RESULT_BUNDLE_SCHEMA, "version": 1,
        "source_rows_sha256": support_files[4]["sha256"], "result_input_plan_sha256": plan_descriptor["sha256"],
        "family_sha256": family["sha256"], "result_manifests": manifests,
        "raw_measurements_sha256": measurement_digest.hexdigest(),
        "member_invocations_sha256": binding._family_invocation_digest(family),
        "member_count": len(family["members"]), "scopes_per_member": len(binding.STATISTICAL_SCOPES),
        "adapter_input": adapter_input, "code_bytes_summary": code_summary,
    }
    result_bundle = write_new(output, "results/sealed-result.bundle", json_bytes(result_bundle_value))
    seal_files = binding._sealed_closure_files(output, record, None, record["workflow"]["records"],
                                                record["workflow"]["phases"], input_plan,
                                                result_bundle, result_bundle_value, adapter_result)
    seal = {"schema": "buster-native-retirement-result-seal-v1", "version": 1, "files": seal_files,
            "root_sha256": binding._canonical_files_digest(seal_files)}
    sealed_value = {"schema": binding.SEALED_RESULT_SCHEMA, "version": 1, "status": "sealed-for-independent-replay",
                    "post_aa_binding_sha256": post_descriptor["sha256"],
                    "result_input_plan_sha256": plan_descriptor["sha256"], "family_sha256": family["sha256"],
                    "result_bundle": result_bundle, "seal": seal}
    sealed_descriptor = write_new(output, "workflow/sealed-result.json", json_bytes(sealed_value))
    record["workflow"]["phases"]["sealed_result"] = sealed_descriptor

    publication = definition["publication"]
    downloaded_files = seal_files + [{"name": "workflow.phases.sealed_result", **sealed_descriptor}]
    archive_files = [{key: item[key] for key in ("path", "bytes", "sha256")} for item in downloaded_files]
    archive_manifest = {"schema": "buster-native-retirement-independent-bundle-manifest-v1", "version": 1,
                        "publication_id": publication["publication_id"], "files": archive_files,
                        "root_sha256": binding._canonical_files_digest([{"name": item["path"], **item} for item in archive_files])}
    archive_path = output / "results/downloaded-independent.bundle.tar"
    with tarfile.open(archive_path, "x") as archive:
        manifest_data = json_bytes(archive_manifest)
        info = tarfile.TarInfo("bundle-manifest.json")
        info.size = len(manifest_data)
        archive.addfile(info, io.BytesIO(manifest_data))
        for item in downloaded_files:
            source_path = output.joinpath(*PurePosixPath(item["path"]).parts)
            info = tarfile.TarInfo(item["path"])
            info.size = item["bytes"]
            with source_path.open("rb") as source_stream:
                archive.addfile(info, source_stream)
    archive_bytes, archive_sha256 = sha256_file(archive_path)
    downloaded = {"path": "results/downloaded-independent.bundle.tar", "bytes": archive_bytes,
                  "sha256": archive_sha256}
    publication_value = {"schema": binding.PUBLICATION_SCHEMA, "version": 1,
                         "publisher": publication["publisher"], "release": publication["release"],
                         "run_id": publication["run_id"], "service_id": record["execution"]["service"]["id"],
                         "publication_id": publication["publication_id"], "sealed_result_sha256": result_bundle["sha256"],
                         "published_bundle_sha256": downloaded["sha256"], "downloaded_bundle_sha256": downloaded["sha256"],
                         "replay_result": "independently-replayed"}
    publication_descriptor = write_new(output, "results/performance-publication.json", json_bytes(publication_value))
    replay_value = {"schema": binding.REPLAY_BUNDLE_SCHEMA, "version": 1,
                    "sealed_result_sha256": result_bundle["sha256"],
                    "raw_measurements_sha256": measurement_digest.hexdigest(), "family_sha256": family["sha256"],
                    "member_invocations_sha256": binding._family_invocation_digest(family), "member_count": len(family["members"]),
                    "adapter_command": "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON",
                    "adapter_build_command": build_command, "adapter_toolchain_sha256": toolchain_digest,
                    "adapter_source_sha256": source_closure_digest,
                    "code_bytes_summary_sha256": binding._canonical_json_digest(code_summary),
                    "publication_id": publication["publication_id"], "published_bundle_sha256": downloaded["sha256"],
                    "downloaded_bundle_sha256": downloaded["sha256"], "downloaded_bundle": downloaded,
                    "adapter_result": adapter_result, "publication_receipt": publication_descriptor}
    replay_descriptor = write_new(output, "results/independent-replay.bundle", json_bytes(replay_value))
    independent_value = {"schema": binding.PHASE_SCHEMA["independent_replay"], "version": 1,
                         "status": "independently-replayed", "sealed_result_sha256": sealed_descriptor["sha256"],
                         "replay_bundle": replay_descriptor, "publication_receipt": publication_descriptor}
    independent_descriptor = write_new(output, "workflow/independent-replay.json", json_bytes(independent_value))
    record["workflow"]["phases"]["independent_replay"] = independent_descriptor
    binding_data = json_bytes(record)
    binding_descriptor = write_new(output, "performance-binding.json", binding_data)
    validation = binding.validate(output / binding_descriptor["path"], output,
                                  repository_root=candidate_source)
    validation_descriptor = write_new(output, "performance-validation.json", json_bytes(validation))
    adapter = json.loads(adapter_result_data)
    verdict_value = {"schema": VERDICT_SCHEMA, "version": 1, "decision_id": "native-retirement-performance-v1",
                     "result_id": arguments.result_id, "binding_sha256": binding_descriptor["sha256"],
                     "validation_sha256": validation_descriptor["sha256"],
                     "independent_replay_sha256": independent_descriptor["sha256"], "outcome": outcome(adapter)}
    write_new(output, "native-retirement-performance-verdict.json", json_bytes(verdict_value))
    shutil.rmtree(work)
    total_output = 0
    for directory, names, files in os.walk(output, followlinks=False):
        for name in names:
            if (Path(directory) / name).is_symlink():
                fail("final evidence contains a symbolic-link directory")
        for name in files:
            path = Path(directory) / name
            info = path.stat(follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                fail("final evidence contains an unsafe file")
            total_output += info.st_size
            if total_output > arguments.total_byte_cap:
                fail("final evidence exceeds the immutable total-byte cap")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("run",))
    parser.add_argument("--definition", required=True)
    parser.add_argument("--definition-sha256", required=True)
    parser.add_argument("--decision-id", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--baseline-source", required=True)
    parser.add_argument("--candidate-source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--baseline-id", required=True)
    parser.add_argument("--candidate-id", required=True)
    parser.add_argument("--result-id", required=True)
    parser.add_argument("--rounds", type=int, required=True)
    parser.add_argument("--minimum-pairs-per-round", type=int, required=True)
    parser.add_argument("--maximum-pairs-per-round", type=int, required=True)
    parser.add_argument("--warmups-per-variant", type=int, required=True)
    parser.add_argument("--partition-record-cap", type=int, required=True)
    parser.add_argument("--partition-count-cap", type=int, required=True)
    parser.add_argument("--total-record-cap", type=int, required=True)
    parser.add_argument("--total-byte-cap", type=int, required=True)
    parser.add_argument("--require-aa-admission", action="store_true")
    parser.add_argument("--require-paired-ab", action="store_true")
    parser.add_argument("--require-sealed-result", action="store_true")
    parser.add_argument("--require-independent-replay", action="store_true")
    arguments = parser.parse_args()
    required = (arguments.decision_id == "native-retirement-performance-v1" and arguments.require_aa_admission
                and arguments.require_paired_ab and arguments.require_sealed_result and arguments.require_independent_replay
                and arguments.rounds == 2 and arguments.minimum_pairs_per_round == 60
                and arguments.maximum_pairs_per_round == 254 and arguments.warmups_per_variant == 2
                and arguments.partition_record_cap == 16 * 1024 * 1024
                and arguments.partition_count_cap == 3 and arguments.total_record_cap == 39_518_208
                and arguments.total_byte_cap == MAX_TOTAL_BYTES)
    if not required:
        fail("fixed native-retirement policy is incomplete")
    run(arguments)


if __name__ == "__main__":
    try:
        main()
    except (ExperimentError, OSError, ValueError, KeyError, sqlite3.Error) as error:
        print(f"native_retirement_performance: {error}", file=os.sys.stderr)
        raise SystemExit(1)
