#!/usr/bin/env python3
"""Replay native closure qualification evidence as bounded data; launch nothing.

Expected pins include base/base_tree/head/head_tree plus absolute root, output,
trusted_lab and python paths and their trusted_lab_sha256/python_sha256 hashes.
Each legacy/snapshot arm supplies prepared, manifest, workload, ledger, files,
and the declared series. files maps immediate native artifact basenames to bytes.
Snapshot also supplies closure records and closure_manifests for the three real
snapshot/restore/verify transfers. Shared dispatch owns admission and raw reads.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import re
from compiler_receipt import PROFILE, THROUGHPUT_PROFILE, classify, classify_throughput, validate_closure

SELECTOR = "compiler-baseline-closure-qualification-v1"
REQUEST_LINE = "profile: " + SELECTOR
SCHEMA = "buster-compiler-closure-qualification-v1"
PREPARATION_SCHEMA = "buster-compiler-preparation-v1"
COST_SCHEMA = "buster-compiler-preparation-cost-v1"
COST_SCOPE = "initialization+preparation_execute+preparation_write"
OWNERSHIP_SCHEMA = "buster-native-qualification-supervisor-v1"
IDENTITY = ("base", "base_tree", "head", "head_tree")
HASH = re.compile(r"[a-f0-9]{64}\Z")
COMMIT = re.compile(r"[a-f0-9]{40}\Z")
UINT = re.compile(r"(?:0|[1-9][0-9]*)\Z")
MEMBER_LIMIT = 8 * 1024 * 1024
LOG_LIMIT = 1 * 1024 * 1024
FILE_COUNT_LIMIT = 768
TOTAL_LIMIT = 256 * 1024 * 1024
BYTE_LIMIT = 8 << 30
TIME_LIMIT_US = 90 * 60 * 1_000_000
SERIES = (("legacy", "ab", False), ("legacy", "immutable-aa", True),
          ("snapshot", "ab", False), ("snapshot", "immutable-aa", True),
          ("snapshot", "cross-build-aa", True))
TOOLS = ("CMAKE_C_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM", "clang", "cmake", "ninja", "tcc")
OPTIONAL_TOOLS = ("ld", "ld.lld", "mold")
BINDINGS = {"bootstrap_config", "bootstrap_marker", "bootstrap_artifact", "resource", *TOOLS, *OPTIONAL_TOOLS}
WORKLOAD_COMMAND = "IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT"


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def uint(value: object, maximum: int = (1 << 64) - 1) -> bool:
    return type(value) is int and 0 <= value <= maximum


def digest(value: object) -> bool:
    return isinstance(value, str) and HASH.fullmatch(value) is not None


def absolute(value: object) -> bool:
    return isinstance(value, str) and value.startswith("/") and value != "/" and \
        all(part not in ("", ".", "..") for part in value[1:].split("/")) and \
        all(ord(char) >= 32 and ord(char) != 127 for char in value)


def overlaps(first: str, second: str) -> bool:
    return first == second or first.startswith(second + "/") or second.startswith(first + "/")


def exact(value: object, wanted: object) -> bool:
    return type(value) is type(wanted) and value == wanted


def number(value: object, wanted: float) -> bool:
    return type(value) in (int, float) and math.isfinite(value) and value == wanted


def text(raw: object, limit: int = MEMBER_LIMIT) -> str:
    if not isinstance(raw, bytes) or not 0 < len(raw) <= limit:
        raise ValueError("missing or oversized byte member")
    value = raw.decode("utf-8")
    if not value.endswith("\n") or "\r" in value or "\0" in value:
        raise ValueError("noncanonical text member")
    return value


def json_object(raw: object) -> dict:
    if not isinstance(raw, bytes) or not 0 < len(raw) <= MEMBER_LIMIT:
        raise ValueError("missing or oversized JSON member")
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("nonfinite JSON constant")
    result = json.loads(raw.decode("utf-8"), object_pairs_hook=unique, parse_constant=invalid)
    if not isinstance(result, dict):
        raise ValueError("JSON member is not an object")
    return result


def profile() -> dict:
    return {"name": SELECTOR, "preparation": ["legacy-rebuild", "snapshot-v1"],
            "series": [list(item) for item in SERIES], "lab": copy.deepcopy(PROFILE),
            "corpus": copy.deepcopy(THROUGHPUT_PROFILE), "ownership": OWNERSHIP_SCHEMA,
            "outer_timeout_minutes": 90, "retry": False, "default_activated": False}


def native_command(trusted_root, root, output, identity, lab, python) -> list[str]:
    """Fixed argv only; caller owns admission, cancellation and cleanup."""
    return [str(trusted_root / "build.sh"), "compiler_closure", "qualify", str(root), str(output),
            *(identity[key] for key in IDENTITY), str(lab), str(python)]


def preparation_phases(arm: str, arm_count: int = 2) -> tuple[str, ...]:
    def build(role):
        return [role + suffix for suffix in ("-checkout", "-generate", "-build", "-build-verify")]
    if arm_count not in (2, 3) or (arm_count == 3 and arm != "snapshot"):
        raise ValueError("unsupported preparation arm count/policy")
    phases = ["pins", *(["secondary-pins"] if arm_count == 3 else []), "reset-checkout", "reset-tracked-source", "reset-build-cache",
              *build("baseline"), "baseline-freeze"]
    if arm == "snapshot":
        phases.append("closure-snapshot")
    phases += [*build("candidate"), "candidate-freeze"]
    if arm_count == 3:
        phases += [*build("candidate2"), "candidate2-freeze"]
    if arm == "legacy":
        phases += [*build("baseline-closure"), "baseline-corpus-prepare"]
    elif arm == "snapshot":
        phases += ["baseline-restore-checkout", "closure-restore", "closure-verify"]
    else:
        raise ValueError("unsupported preparation arm")
    return tuple(phases + ["baseline-corpus-verify", "prepared"])


def qualification_phases(arm: str) -> tuple[str, ...]:
    phases = list(preparation_phases(arm))
    if arm == "snapshot":
        phases.append("matched-frozen-workload")
    for member, name, _ in SERIES:
        if member == arm:
            phases += [name + suffix for suffix in ("-lab-binaries-before", "-lab", "-lab-binaries-after",
                       "-throughput-binaries-before", "-throughput", "-throughput-binaries-after", "-post")]
    return tuple(phases)


def parse_ledger(record: dict, raw: object, wanted: tuple[str, ...] | None = None) -> tuple[str, list[dict]]:
    value = text(raw)
    if record.get("ledger_sha256") != sha(raw):
        raise ValueError("phase ledger hash mismatch")
    lines = value.splitlines()
    if len(lines) < 9 or lines[0] != "BUSTER_COMPILER_PREPARATION_LEDGER_V1":
        raise ValueError("phase ledger header missing")
    keys = ("root", "policy", "base", "base_tree", "head", "head_tree")
    headers = {}
    for key, line in zip(keys, lines[1:7]):
        fields = line.split("\t")
        if len(fields) != 2 or fields[0] != key:
            raise ValueError("phase ledger header changed")
        headers[key] = fields[1]
    root = headers["root"]
    if not absolute(root) or sha(root.encode()) != record.get("root_sha256") or \
            headers["policy"] != record.get("policy") or any(headers[key] != record.get(key) for key in IDENTITY):
        raise ValueError("phase ledger identity mismatch")
    pending = None
    stages = []
    secondary = 0
    three = record.get("arm_count") == 3
    if three and (record.get("policy") != "snapshot-v1" or any(not isinstance(record.get(key), str) or
            not COMMIT.fullmatch(record[key]) for key in ("secondary_head", "secondary_tree"))):
        raise ValueError("three-arm secondary pin declaration malformed")
    previous = 0
    for line in lines[7:]:
        fields = line.split("\t")
        if fields[0] == "start" and len(fields) == 4 and pending is None and \
                UINT.fullmatch(fields[1]) and UINT.fullmatch(fields[3]):
            ordinal, started = int(fields[1]), int(fields[3])
            if ordinal != len(stages) + 1 or not 0 < started <= (1 << 64) - 1 or started < previous or \
                    not re.fullmatch(r"[a-z][a-z0-9-]{0,95}", fields[2]):
                raise ValueError("phase ledger start reordered or malformed")
            pending = {"stage": ordinal, "phase": fields[2], "start": started}
        elif fields[0] in ("secondary_head", "secondary_tree"):
            key = ("secondary_head", "secondary_tree")[secondary] if secondary < 2 else None
            if not three or pending is None or pending["stage"] != 2 or pending["phase"] != "secondary-pins" or \
                    len(fields) != 2 or fields[0] != key or fields[1] != record.get(key):
                raise ValueError("secondary pin ledger row absent, repeated or mismatched")
            secondary += 1
        elif fields[0] == "finish" and len(fields) == 7 and pending is not None and \
                all(UINT.fullmatch(fields[index]) for index in (1, 3, 4, 6)):
            ordinal, finished, elapsed, status = (int(fields[index]) for index in (1, 3, 4, 6))
            if ordinal != pending["stage"] or fields[2] != pending["phase"] or finished < pending["start"] or \
                    finished > (1 << 64) - 1 or elapsed != finished - pending["start"] or \
                    fields[5] != "complete" or status != 0 or \
                    (pending["phase"] == "secondary-pins" and (not three or secondary != 2)):
                raise ValueError("phase ledger finish failed or mismatched")
            stages.append(dict(pending, finish=finished, elapsed=elapsed))
            previous = finished
            pending = None
        else:
            raise ValueError("phase ledger truncated, foreign or duplicate row")
    if pending is not None or secondary != (2 if three else 0) or not uint(record.get("stage_count"), 96) or \
            not 0 < record["stage_count"] == len(stages) <= 96 or \
            (wanted is not None and tuple(row["phase"] for row in stages) != wanted):
        raise ValueError("phase ledger stage_count or fixed plan mismatch")
    duration = record.get("duration_us")
    if not uint(duration, TIME_LIMIT_US) or duration < stages[-1]["finish"] - stages[0]["start"]:
        raise ValueError("preparation duration does not contain every phase")
    return root, stages


def ledger_reasons(record: dict, raw: object) -> list[str]:
    try:
        parse_ledger(record, raw)
        return []
    except (ValueError, UnicodeError, TypeError, KeyError, IndexError):
        return ["preparation ledger incomplete, reordered, failed or mismatched"]


def manifest_inventory(raw: object, expected: dict) -> tuple[str, dict, dict, list[list[str]]]:
    lines = text(raw).splitlines()
    if len(lines) < 5 or lines[:4] != ["BUSTER_COMPILER_CLOSURE_V1", "root\t" + expected["root"],
                                     "base\t" + expected["base"], "tree\t" + expected["base_tree"]]:
        raise ValueError("raw manifest source/tree/root header mismatch")
    root = expected["root"]
    if not absolute(root):
        raise ValueError("raw manifest root malformed")
    rows, bindings, ordered, total = {}, {}, [], 0
    for line in lines[4:-1]:
        fields = line.split("\t")
        if fields[0] == "binding":
            if len(fields) != 3 or fields[1] not in BINDINGS or fields[1] in bindings:
                raise ValueError("raw manifest binding malformed or repeated")
            bindings[fields[1]] = fields[2]
            ordered.append(fields)
            continue
        if len(fields) != 8 or fields[0] not in ("source", "build", "bootstrap", "tool", "resource") or \
                fields[1] not in ("F", "D") or not all(UINT.fullmatch(item) for item in fields[2:6]) or \
                any(part in ("", ".", "..") for part in fields[7].split("/")) or \
                any(ord(char) < 32 or ord(char) == 127 for char in fields[7]):
            raise ValueError("raw manifest record malformed or unsafe")
        key = fields[0], fields[7]
        mode, seconds, nanos, size = (int(item) for item in fields[2:6])
        if key in rows or mode > 0o7777 or seconds > (1 << 64) - 1 or nanos >= 1_000_000_000 or size > BYTE_LIMIT or \
                (fields[1] == "F" and not digest(fields[6])) or \
                (fields[1] == "D" and (fields[6] != "-" or seconds or nanos or size)):
            raise ValueError("raw manifest record duplicate or invalid identity")
        rows[key] = fields
        ordered.append(fields)
        total += size
        if len(rows) > 65536 or total > BYTE_LIMIT:
            raise ValueError("raw manifest inventory bound exceeded")
    if lines[-1] != f"END\t{len(rows)}\t{total}" or set(bindings) != BINDINGS:
        raise ValueError("raw manifest inventory/footer/binding set mismatch")
    for key in (("source", "build.c"), ("source", "build.sh"), ("source", "tools/bootstrap_driver.sh"),
                ("build", "CMakeCache.txt"), ("build", "Release/ide"), ("build", "throughput-tools/throughput")):
        if key not in rows or rows[key][1] != "F" or int(rows[key][5]) == 0:
            raise ValueError("raw manifest critical source/compiler/configuration/harness missing")
    for key in (("source", "build.sh"), ("build", "Release/ide"), ("build", "throughput-tools/throughput")):
        if not int(rows[key][2]) & 0o111:
            raise ValueError("raw manifest required executable mode absent")
    for key in TOOLS + OPTIONAL_TOOLS:
        binding = bindings[key]
        row = rows.get(("tool", key))
        if key in OPTIONAL_TOOLS and binding == "absent":
            if row is not None:
                raise ValueError("absent optional tool has an inventory row")
        elif not absolute(binding) or row is None or row[1] != "F" or \
                not int(row[2]) & 0o111 or int(row[5]) == 0:
            raise ValueError("raw manifest configured tool executable identity missing")
    if not absolute(bindings["resource"]) or not any(key[0] == "resource" for key in rows):
        raise ValueError("raw manifest configured resource closure missing")
    configuration, artifact, marker = (bindings[key] for key in ("bootstrap_config", "bootstrap_artifact", "bootstrap_marker"))
    if not digest(configuration) or not artifact.startswith("posix/" + configuration + "/") or artifact.count("/") != 2 or \
            marker != artifact + ".complete" or any(part in ("", ".", "..") for part in artifact.split("/")):
        raise ValueError("raw manifest actual bootstrap producer binding malformed")
    for path in (artifact, marker):
        row = rows.get(("bootstrap", path))
        if row is None or row[1] != "F" or int(row[5]) == 0:
            raise ValueError("raw manifest actual bootstrap producer pair absent")
    if not int(rows["bootstrap", artifact][2]) & 0o111:
        raise ValueError("raw manifest selected bootstrap artifact is not executable")
    return root, rows, bindings, ordered


def normalized_workload(raw: object, expected: dict) -> bytes:
    """Exactly mirror native normalization; preserve every raw byte separately."""
    _, rows, bindings, ordered = manifest_inventory(raw, expected)
    result = ["BUSTER_COMPILER_CLOSURE_V1", "root\t" + expected["root"],
              "base\t" + expected["base"], "tree\t" + expected["base_tree"]]
    for fields in ordered:
        if fields[0] == "binding":
            if fields[1] not in ("bootstrap_marker", "bootstrap_artifact"):
                result.append("\t".join(fields))
            continue
        path = fields[7]
        generated = fields[0] == "build" and (path in ("generated", "include", "CMakeCache.txt",
                    "compile_commands.json", "throughput-tools/throughput") or
                    path.startswith(("generated/", "include/")) or path.endswith((".h", ".inc", ".c", ".cmake", ".ninja", ".rsp")))
        if fields[0] in ("source", "resource", "tool") or generated:
            result.append("\t".join(fields[index] for index in (0, 1, 2, 5, 6, 7)))
    artifact_sha256 = rows["bootstrap", bindings["bootstrap_artifact"]][6]
    result += ["producer\tconfiguration\t" + bindings["bootstrap_config"], "producer\tartifact_sha256\t" + artifact_sha256]
    encoded = ("\n".join(result) + "\n").encode()
    if len(encoded) > MEMBER_LIMIT:
        raise ValueError("normalized workload oversized")
    return encoded


def parse_argv(raw: object) -> list[str]:
    if not isinstance(raw, bytes) or not 0 < len(raw) <= LOG_LIMIT:
        raise ValueError("child argv missing or oversized")
    result, offset = [], 0
    while offset < len(raw):
        colon = raw.find(b":", offset, min(len(raw), offset + 22))
        prefix = raw[offset:colon] if colon >= 0 else b""
        if not prefix or not UINT.fullmatch(prefix.decode("ascii")):
            raise ValueError("child argv length malformed")
        count = int(prefix)
        start, end = colon + 1, colon + 1 + count
        if end >= len(raw) or raw[end:end + 1] != b"\n" or b"\0" in raw[start:end]:
            raise ValueError("child argv member truncated")
        result.append(raw[start:end].decode("utf-8"))
        if len(result) > 128:
            raise ValueError("child argv count bound exceeded")
        offset = end + 1
    return result


def cleanup_reasons(record: object) -> list[str]:
    if not isinstance(record, dict) or record.get("schema") != OWNERSHIP_SCHEMA or record.get("cleanup_proven") is not True:
        return ["native child cleanup proof missing or unsupported"]
    if not uint(record.get("duration_us"), TIME_LIMIT_US) or not uint(record.get("waves")) or \
            any(not exact(record.get(key), 0) for key in
                ("signalled", "reaped", "timed_out", "cancelled", "reservation_retained", "ownership_lost")):
        return ["native child timeout/cancellation/ownership failure or nominal orphan cleanup"]
    return []


def arm_files(arm: dict) -> dict[str, bytes]:
    files = arm.get("files")
    if not isinstance(files, dict) or not 0 < len(files) <= FILE_COUNT_LIMIT:
        raise ValueError("native artifact file map missing or oversized")
    total = 0
    for name, raw in files.items():
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,160}", name) or \
                not isinstance(raw, bytes) or len(raw) > MEMBER_LIMIT:
            raise ValueError("native artifact basename/member malformed or oversized")
        total += len(raw)
        if total > TOTAL_LIMIT:
            raise ValueError("native artifact total byte bound exceeded")
    return files


def frozen_binary(record: dict, arm: str, role: str, expected: dict) -> tuple[str, str, int, int]:
    basename = "ide-base" if role == "baseline" else "ide-cand"
    return (f"{expected['output']}/{arm}/bin/{basename}", record[role + "_sha256"],
            record[role + "_bytes"], record[role + "_mode"])


def child_commands(arm: str, expected: dict, prepared: dict, qualification: bool = True) -> dict[str, list[str]]:
    root = expected["root"]
    output = expected["output"] + "/" + arm if qualification else expected["output"]
    wrapper = root + "/build.sh"
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null", "-C", root]
    result = {"reset-checkout": [*git, "checkout", "--quiet", "--detach", expected["base"]],
              "reset-tracked-source": [*git, "reset", "--hard", "--quiet", expected["base"]],
              "reset-build-cache": [*git, "clean", "-fdx"]}
    roles = [("baseline", expected["base"]), ("candidate", expected["head"])]
    if prepared.get("arm_count") == 3:
        roles.append(("candidate2", expected["secondary_head"]))
    for role, commit in roles:
        result[role + "-checkout"] = [*git, "checkout", "--quiet", "--detach", commit]
        result[role + "-generate"] = [wrapper, "generate", "--cc", "clang", "--no-include-tests"]
        result[role + "-build"] = [wrapper, "build", "--config", "Release", "-t", "ide"]
    if arm == "legacy":
        result["baseline-closure-checkout"] = [*git, "checkout", "--quiet", "--detach", expected["base"]]
        result["baseline-closure-generate"] = [wrapper, "generate", "--cc", "clang", "--no-include-tests"]
        result["baseline-closure-build"] = [wrapper, "build", "--config", "Release", "-t", "ide"]
        result["baseline-corpus-prepare"] = [wrapper, "bench_throughput", "help"]
    else:
        result["baseline-restore-checkout"] = [*git, "checkout", "--quiet", "--detach", expected["base"]]
    for member, name, same in (SERIES if qualification else ()):
        if member != arm:
            continue
        base_arm = "legacy" if name == "cross-build-aa" else arm
        baseline = f"{expected['output']}/{base_arm}/bin/ide-base"
        candidate = f"{output}/bin/" + ("ide-base" if same else "ide-cand")
        identical = ["--require-identical-output"] if same else []
        result[name + "-lab"] = [expected["python"], "-B", expected["trusted_lab"], "compare",
            "--baseline", baseline, "--candidate", candidate, "--repo-root", root, "--cpu", "2",
            "--output", output + "/" + name + "-lab", "--target-minutes", "10", "--warmups", "1", *identical]
        result[name + "-throughput"] = [root + "/build/throughput-tools/throughput", "run", "--baseline", baseline,
            "--candidate", candidate, "--output", output + "/" + name + "-throughput", "--baseline-id", expected["base"],
            "--candidate-id", expected["base" if same else "head"], *THROUGHPUT_PROFILE["arguments"], *identical]
    return result


def freeze_reasons(prepared: dict, files: dict, expected: dict) -> list[str]:
    reasons = []
    roles = ("baseline", "candidate", "candidate2") if prepared.get("arm_count") == 3 else ("baseline", "candidate")
    for role in roles:
        try:
            raw = files[role + ".binary.json"]
            if sha(raw) != prepared.get(role + "_receipt_sha256"):
                raise ValueError("freeze receipt hash changed")
            record = json_object(raw)
            wanted = {"schema": "buster-compiler-frozen-binary-v1", "state": "complete", "role": role,
                      "commit": expected["base" if role == "baseline" else ("secondary_head" if role == "candidate2" else "head")],
                      "tree": expected["base_tree" if role == "baseline" else ("secondary_tree" if role == "candidate2" else "head_tree")],
                      "sha256": prepared[role + "_sha256"], "bytes": prepared[role + "_bytes"],
                      "mode": prepared[role + "_mode"], "cache_sha256": prepared[role + "_cache_sha256"]}
            cache = files[role + ".CMakeCache.txt"]
            if any(not exact(record.get(key), value) for key, value in wanted.items()) or \
                    not cache or record.get("cache_sha256") != sha(cache) or \
                    not exact(record.get("cache_bytes"), len(cache)) or \
                    not uint(record.get("cache_mode"), 0o7777) or not record["cache_mode"] & 0o444:
                raise ValueError("freeze executable/cache identity mismatch")
        except (ValueError, UnicodeError, TypeError, KeyError, IndexError):
            reasons.append(role + " native frozen binary/cache receipt missing or changed")
    return reasons


def binary_check_reasons(raw: object, wanted: tuple[tuple, tuple]) -> list[str]:
    try:
        lines = text(raw, LOG_LIMIT).splitlines()
        if len(lines) != 3 or lines[0] != "BUSTER_COMPILER_FROZEN_BINARY_CHECK_V1":
            raise ValueError("binary check header/population mismatch")
        for line, identity in zip(lines[1:], wanted):
            path, digest_value, size, mode = identity
            fields = line.split("\t")
            if fields != [path, digest_value, str(size), str(mode), digest_value, str(size), str(mode), "complete"]:
                raise ValueError("binary check expected/observed identity mismatch")
        return []
    except (ValueError, UnicodeError, TypeError, KeyError, IndexError):
        return ["external frozen binary byte/size/mode check missing or changed"]


def preparation_cost(record: dict, prepared_raw: bytes, cost_raw: bytes,
                     stages: list[dict] | None = None) -> dict:
    """Validate the complete native operation cost against its raw prep receipt.

    This scope includes preparation_write but excludes the cost receipt's own
    publication and final qualification export. It contains no stage-sum estimate.
    """
    cost = json_object(cost_raw)
    fields = {"schema", "state", "policy", "arm_count", *IDENTITY, "secondary_head", "secondary_tree",
              "root_sha256", "prepared_receipt_sha256", "scope", "ownership_schema", "cleanup_proven",
              "complete_cost_available", "initialization_us", "execute_us", "finalize_us", "total_us"}
    fixed = {"schema": COST_SCHEMA, "state": "complete", "scope": COST_SCOPE,
             "ownership_schema": OWNERSHIP_SCHEMA, "cleanup_proven": True, "complete_cost_available": True}
    if set(cost) != fields or any(not exact(cost.get(key), value) for key, value in fixed.items()) or \
            not isinstance(record, dict) or record.get("schema") != PREPARATION_SCHEMA or \
            record.get("state") != "complete" or record.get("ownership_schema") != OWNERSHIP_SCHEMA or \
            record.get("cleanup_proven") is not True or \
            record.get("policy") not in ("legacy-rebuild", "snapshot-v1") or \
            not any(exact(record.get("arm_count"), count) for count in (2, 3)) or \
            any(not exact(cost.get(key), record.get(key)) for key in
                (*IDENTITY, "policy", "arm_count", "secondary_head", "secondary_tree", "root_sha256")) or \
            any(not isinstance(cost.get(key), str) or not COMMIT.fullmatch(cost[key]) for key in IDENTITY) or \
            not digest(cost.get("root_sha256")) or not digest(cost.get("prepared_receipt_sha256")) or \
            json_object(prepared_raw) != record or sha(prepared_raw) != cost["prepared_receipt_sha256"]:
        raise ValueError("native whole-operation cost identity/scope/prepared receipt binding mismatch")
    if record["arm_count"] == 3:
        if record["policy"] != "snapshot-v1" or any(not isinstance(cost.get(key), str) or
                not COMMIT.fullmatch(cost[key]) for key in ("secondary_head", "secondary_tree")):
            raise ValueError("native whole-operation cost secondary pins malformed")
    elif cost["secondary_head"] != "" or cost["secondary_tree"] != "":
        raise ValueError("native two-arm whole-operation cost has secondary pins")
    durations = ("initialization_us", "execute_us", "finalize_us")
    if any(not uint(cost.get(key), TIME_LIMIT_US) for key in (*durations, "total_us")) or \
            cost["total_us"] != sum(cost[key] for key in durations):
        raise ValueError("native whole-operation cost duration types/sum malformed")
    if stages is not None:
        prepared_index = next(index for index, row in enumerate(stages) if row["phase"] == "prepared")
        body = [row for row in stages[:prepared_index + 1] if row["phase"] not in ("pins", "secondary-pins")]
        pins = [row for row in stages[:prepared_index + 1] if row["phase"] in ("pins", "secondary-pins")]
        if not body or not pins or cost["initialization_us"] < pins[-1]["finish"] - pins[0]["start"] or \
                cost["execute_us"] < body[-1]["finish"] - body[0]["start"]:
            raise ValueError("native whole-operation cost omits preparation ledger wall span")
    return cost


def preparation_timings(record: dict, ledger: bytes, cost_record: bytes | None = None, *,
                        prepared_receipt: bytes | None = None,
                        receipt_publication_us: int | None = None) -> dict:
    """Replay observed costs; ledger spans remain diagnostic and no job net is inferred.

    Supply raw cost_record and prepared_receipt for the native operation scope.
    receipt_publication_us must come from the validated qualification pointer or
    a trusted outer observation; None keeps the cost receipt's own tail unassigned.
    Final qualification export always remains outside this scope.
    """
    reported = record.get("duration_us") if isinstance(record, dict) else None
    reported = reported if uint(reported, TIME_LIMIT_US) else None
    unavailable = {"available": False, "complete_cost_available": False,
                   "complete_job_cost_available": False,
                   "controller_reported_duration_us": reported, "publication_us": None,
                   "qualification_publication_us": None}
    if not isinstance(record, dict) or record.get("schema") != PREPARATION_SCHEMA or record.get("state") != "complete" or \
            record.get("ownership_schema") != OWNERSHIP_SCHEMA or record.get("cleanup_proven") is not True:
        return unavailable
    try:
        _, stages = parse_ledger(record, ledger)
        prepared_index = next(index for index, row in enumerate(stages) if row["phase"] == "prepared")
        preparation = stages[:prepared_index + 1]
        body = [row for row in preparation if row["phase"] not in ("pins", "secondary-pins")]
        if not body:
            return unavailable
        span = body[-1]["finish"] - body[0]["start"]
        stage_total = sum(row["elapsed"] for row in body)
        timing = {"available": True, "complete_cost_available": False, "complete_job_cost_available": False,
                  "cost_scope": "first post-initial-pins start through prepared.finish",
                  "preparation_span_us": span, "stage_total_us": stage_total,
                  "unassigned_us": span - stage_total,
                  "initial_pins_us": sum(row["elapsed"] for row in preparation if row["phase"] in ("pins", "secondary-pins")),
                  "stages_us": {row["phase"]: row["elapsed"] for row in preparation},
                  "controller_reported_duration_us": reported, "publication_us": None,
                  "qualification_publication_us": None, "snapshot_includes_harness_preparation": True}
        if cost_record is None:
            if prepared_receipt is not None or receipt_publication_us is not None:
                return unavailable
            return timing
        cost = preparation_cost(record, prepared_receipt, cost_record, stages)
        timing.update(complete_cost_available=True, cost_scope=COST_SCOPE,
                      initialization_us=cost["initialization_us"], execute_us=cost["execute_us"],
                      finalize_us=cost["finalize_us"], native_operation_total_us=cost["total_us"],
                      cost_receipt_sha256=sha(cost_record))
        if receipt_publication_us is not None:
            if not uint(receipt_publication_us, TIME_LIMIT_US) or \
                    cost["total_us"] + receipt_publication_us > TIME_LIMIT_US:
                return unavailable
            timing.update(publication_us=receipt_publication_us,
                          total_us=cost["total_us"] + receipt_publication_us,
                          cost_scope=COST_SCOPE + "+cost_receipt_publication")
        return timing
    except (ValueError, UnicodeError, TypeError, KeyError, IndexError, StopIteration, AttributeError, RecursionError):
        return unavailable


def preparation_reasons(expected: dict, arm_name: str, arm: object, wanted_phases: tuple[str, ...],
                        qualification: bool = True, arm_count: int = 2) -> tuple[list[str], dict]:
    reasons = []
    arm = arm if isinstance(arm, dict) else {}
    prepared = arm.get("prepared")
    if not isinstance(prepared, dict):
        return [arm_name + " native preparation receipt missing"], {}
    policy = "legacy-rebuild" if arm_name == "legacy" else "snapshot-v1"
    wanted = {"schema": PREPARATION_SCHEMA, "state": "complete", "policy": policy, "arm_count": arm_count,
              "ownership_schema": OWNERSHIP_SCHEMA, "cleanup_proven": True,
              "root_sha256": sha(expected["root"].encode())}
    roles = ("baseline", "candidate", "candidate2") if arm_count == 3 else ("baseline", "candidate")
    if arm_count == 3:
        wanted.update(secondary_head=expected["secondary_head"], secondary_tree=expected["secondary_tree"])
    else:
        wanted.update(secondary_head="", secondary_tree="", candidate2_sha256="", candidate2_bytes=0,
                      candidate2_mode=0, candidate2_cache_sha256="", candidate2_receipt_sha256="")
    if any(not exact(prepared.get(key), value) for key, value in wanted.items()) or \
            any(prepared.get(key) != expected[key] for key in IDENTITY) or \
            not uint(prepared.get("duration_us"), TIME_LIMIT_US) or \
            any(not digest(prepared.get(key)) for key in ("prepared_manifest_sha256", "frozen_workload_sha256",
                "ledger_sha256", "bootstrap_configuration", "bootstrap_artifact_sha256",
                "baseline_cache_sha256", "candidate_cache_sha256", "baseline_receipt_sha256", "candidate_receipt_sha256")) or \
            any(not digest(prepared.get(role + suffix)) for role in roles for suffix in ("_cache_sha256", "_receipt_sha256")) or \
            any(not digest(prepared.get(role + "_sha256")) or not uint(prepared.get(role + "_bytes"), BYTE_LIMIT) or
                prepared[role + "_bytes"] == 0 or not uint(prepared.get(role + "_mode"), 0o7777) or
                not prepared[role + "_mode"] & 0o111 for role in (*roles, "harness")):
        return [arm_name + " native preparation identity/plan/binary/ownership receipt malformed"], {}
    try:
        files = arm_files(arm)
        root, stages = parse_ledger(prepared, arm.get("ledger"), wanted_phases)
        if root != expected["root"] or files.get("phases.tsv") != arm.get("ledger") or \
                json_object(files.get("prepared.json")) != prepared:
            raise ValueError("preparation file/ledger binding mismatch")
        cost_raw = files.get("preparation-cost.json")
        cost = preparation_cost(prepared, files.get("prepared.json"), cost_raw, stages)
        manifest = arm.get("manifest")
        if not isinstance(manifest, bytes) or prepared["prepared_manifest_sha256"] != sha(manifest) or \
                files.get("prepared.manifest.tsv") != manifest:
            raise ValueError("preparation raw manifest hash/file mismatch")
        _, rows, bindings, _ = manifest_inventory(manifest, expected)
        workload = normalized_workload(manifest, expected)
        if arm.get("workload") != workload or files.get("prepared.workload.tsv") != workload or \
                prepared["frozen_workload_sha256"] != sha(workload):
            raise ValueError("preparation normalized workload does not reconstruct from raw inventory")
        if prepared["bootstrap_configuration"] != bindings["bootstrap_config"] or \
                prepared["bootstrap_artifact_sha256"] != rows["bootstrap", bindings["bootstrap_artifact"]][6]:
            raise ValueError("preparation actual bootstrap producer/configuration mismatch")
        harness = rows["build", "throughput-tools/throughput"]
        if (prepared["harness_sha256"], prepared["harness_bytes"], prepared["harness_mode"]) != \
                (harness[6], int(harness[5]), int(harness[2])):
            raise ValueError("preparation actual native corpus identity mismatch")
        if prepared["baseline_cache_sha256"] != rows["build", "CMakeCache.txt"][6]:
            raise ValueError("preparation frozen baseline cache identity mismatch")
        if arm_name == "snapshot":
            compiler = rows["build", "Release/ide"]
            if (prepared["baseline_sha256"], prepared["baseline_bytes"], prepared["baseline_mode"]) != \
                    (compiler[6], int(compiler[5]), int(compiler[2])):
                raise ValueError("restored snapshot baseline executable identity mismatch")
        elif prepared.get("snapshot_digest") != "":
            raise ValueError("legacy preparation synthesized a snapshot digest")
        reasons.extend(arm_name + ": " + item for item in freeze_reasons(prepared, files, expected))
        for role in roles:
            cache = files.get(role + ".CMakeCache.txt", b"")
            if not isinstance(cache, bytes) or b"BUSTER_INCLUDE_TESTS:BOOL=OFF\n" not in cache or \
                    ("CMAKE_HOME_DIRECTORY:INTERNAL=" + expected["root"] + "\n").encode() not in cache:
                reasons.append(arm_name + "/" + role + " configured cache is not matched-root tests-off")
        commands = child_commands(arm_name, expected, prepared, qualification)
        cleanup_names = set()
        proof_names = set()
        for stage in stages:
            phase, ordinal = stage["phase"], stage["stage"]
            if phase in commands:
                stem = f"{ordinal}-{phase}"
                cleanup_names.add(stem + ".cleanup.json")
                if parse_argv(files.get(stem + ".argv")) != commands[phase]:
                    reasons.append(arm_name + "/" + phase + " child argv changed the pinned recipe")
                cleanup = json_object(files.get(stem + ".cleanup.json"))
                reasons.extend(arm_name + "/" + phase + ": " + item for item in cleanup_reasons(cleanup))
                for stream in ("stdout", "stderr"):
                    raw_stream = files.get(stem + "." + stream)
                    if not isinstance(raw_stream, bytes) or len(raw_stream) > LOG_LIMIT:
                        reasons.append(arm_name + "/" + phase + " bounded child " + stream + " missing")
            if phase.endswith("-binaries-before") or phase.endswith("-binaries-after"):
                proof_names.add(f"{ordinal}-{phase}.binaries.tsv")
        if {name for name in files if name.endswith(".cleanup.json")} != cleanup_names:
            reasons.append(arm_name + " child cleanup proof set differs from the fixed command plan")
        if {name for name in files if name.endswith(".binaries.tsv")} != proof_names:
            reasons.append(arm_name + " external binary proof set differs from the fixed measurement plan")
        for member, name, _ in (SERIES if qualification else ()):
            if member == arm_name:
                if files.get(name + "-post.manifest.tsv") != manifest or files.get(name + "-post.workload.tsv") != workload:
                    reasons.append(arm_name + "/" + name + " post-measurement source/toolchain closure changed")
        if arm_name == "snapshot":
            closure = arm.get("closure")
            manifests = arm.get("closure_manifests")
            if not isinstance(closure, dict) or not isinstance(manifests, dict) or \
                    set(closure) != {"snapshot", "restore", "verify"} or set(manifests) != {"snapshot", "restore", "verify"}:
                raise ValueError("actual snapshot/restore/verify records/manifests missing")
            for operation in ("snapshot", "restore", "verify"):
                if json_object(files.get("closure-" + operation + ".json")) != closure[operation] or \
                        files.get("closure-" + operation + ".json.manifest.tsv") != manifests[operation] or \
                        manifests[operation] != manifest:
                    raise ValueError("actual native closure receipt/raw manifest changed")
            if prepared.get("snapshot_digest") != sha(manifest):
                raise ValueError("prepared snapshot digest differs from actual transfers")
            replay = {"preparation_policy": "snapshot-v1", "identity": {key: expected[key] for key in IDENTITY},
                      "binaries": {"baseline": {"sha256": prepared["baseline_sha256"]}},
                      "closure": {"policy": "snapshot-v1", "fallback": None, **closure}}
            reasons.extend(arm_name + ": " + item for item in validate_closure(replay, manifests, expected_policy="snapshot-v1"))
        elif arm.get("closure") not in (None, {}) or arm.get("closure_manifests") not in (None, {}):
            reasons.append("legacy preparation unexpectedly contains a snapshot transfer")
        return reasons, {"record": prepared, "files": files, "stages": stages, "rows": rows,
                         "bindings": bindings, "workload": workload, "manifest": manifest,
                         "cost": cost, "cost_raw": cost_raw}
    except (ValueError, UnicodeError, TypeError, KeyError, IndexError, AttributeError, RecursionError) as error:
        diagnostic = (type(error).__name__ + ": " + str(error))[:200]
        diagnostic = "".join(char if ord(char) >= 32 and ord(char) != 127 else " " for char in diagnostic)
        reasons.append(arm_name + " preparation raw inventory/ledger/configuration/proof replay failed: " + diagnostic)
        return reasons, {}


def lab_reasons(row: dict, expected: dict, binaries: tuple[tuple, tuple], same_source: bool) -> list[str]:
    reasons = []
    lab = row.get("lab")
    lab = lab if isinstance(lab, dict) else {}
    config = lab.get("config") if isinstance(lab.get("config"), dict) else {}
    fixed = {"command": WORKLOAD_COMMAND, "repo_root": expected["root"], "cpu": 2, "perf": "perf", "pairs": None,
             "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
             "require_identical_output": same_source, "extra": [], "canonical_inline_pair": False,
             "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True}
    if not exact(lab.get("version"), 1) or lab.get("mode") != "compare" or \
            any(not exact(config.get(key), value) for key, value in fixed.items()) or \
            not number(config.get("target_minutes"), 10) or not number(config.get("min_effect_percent"), 0.5) or \
            "process_ownership" in config or any(not isinstance(key, str) or key.startswith("BUSTER_MEASUREMENT_") for key in config):
        reasons.append("historical lab population/configuration/launch settings changed")
    variants = lab.get("variants") if isinstance(lab.get("variants"), dict) else {}
    for key, role, identity in zip(("a", "b"), ("baseline", "candidate"), binaries):
        variant = variants.get(key)
        path, digest_value, size, _ = identity
        if not isinstance(variant, dict) or variant.get("role") != role or variant.get("ide") != path or \
                variant.get("sha256") != digest_value or not exact(variant.get("size_bytes"), size):
            reasons.append(role + " raw lab variant does not match the frozen executable")
    summary = row.get("summary")
    plan = lab.get("plan") if isinstance(lab.get("plan"), dict) else {}
    summary_plan = summary.get("plan") if isinstance(summary, dict) and isinstance(summary.get("plan"), dict) else {}
    pairs = summary_plan.get("pairs")
    reason = plan.get("reason")
    if not uint(pairs, 1000) or pairs < 10 or pairs % 2 or not exact(plan.get("pairs"), pairs) or \
            plan.get("order") != "ABBA" or plan.get("fresh_copy") is not True or \
            not isinstance(reason, str) or not reason.startswith("--target-minutes 10: ") or \
            "(median of 2 pilot pairs)" not in reason or \
            not reason.endswith(f"-> {pairs} pairs (clamped to 10..1000, whole ABBA blocks)"):
        reasons.append("lab adaptive pilot/whole-ABBA population plan changed or truncated")
    if same_source:
        verdict = summary.get("verdict") if isinstance(summary, dict) and isinstance(summary.get("verdict"), dict) else {}
        low, high = verdict.get("ci_low"), verdict.get("ci_high")
        if summary is None or not isinstance(summary, dict) or summary.get("outputs_identical") is not True or \
                verdict.get("outcome") != "no detectable difference" or \
                type(low) not in (int, float) or type(high) not in (int, float) or not low <= 1 <= high:
            reasons.append("same-source A/A control lacks identical output and a no-effect wall interval")
    return reasons


def validate(expected: dict, receipt: object, bundles: object) -> list[str]:
    """Replay exact native plan and all saved controls; exit0 alone is insufficient."""
    if not isinstance(expected, dict) or \
            any(not isinstance(expected.get(key), str) or not COMMIT.fullmatch(expected[key]) for key in IDENTITY) or \
            any(not absolute(expected.get(key)) for key in ("root", "output", "trusted_lab", "python")) or \
            any(not digest(expected.get(key)) for key in ("trusted_lab_sha256", "python_sha256")):
        return ["trusted native qualification source/root/output/lab/Python pins missing or malformed"]
    if overlaps(expected["root"], expected["output"]):
        return ["trusted qualification ROOT/output directories overlap"]
    wanted = {"schema": SCHEMA, "profile": SELECTOR, "state": "complete", "default_activated": False,
              "ownership_schema": OWNERSHIP_SCHEMA, "cleanup_proven": True, "cpu": 2, "target_minutes": 10,
              "warmups": 1, "seed": 20261003, "min_effect_percent": 0.5, "planned_labs": 5, "planned_corpora": 5,
              "root_sha256": sha(expected["root"].encode()), "trusted_lab_sha256": expected["trusted_lab_sha256"],
              "python_sha256": expected["python_sha256"]}
    if not isinstance(receipt, dict) or any(not exact(receipt.get(key), value) for key, value in wanted.items()) or \
            any(receipt.get(key) != expected[key] for key in IDENTITY) or \
            not uint(receipt.get("duration_us"), TIME_LIMIT_US) or not isinstance(bundles, dict) or \
            "qualification_publication_us" not in receipt or receipt["qualification_publication_us"] is not None or \
            not isinstance(receipt.get("preparation_costs"), dict) or set(receipt["preparation_costs"]) != {"legacy", "snapshot"}:
        return ["native preparation qualification identity/plan/ownership incomplete or unsupported"]
    reasons, arms = [], {}
    for arm in ("legacy", "snapshot"):
        errors, parsed = preparation_reasons(expected, arm, bundles.get(arm), qualification_phases(arm))
        reasons.extend(errors)
        if parsed:
            arms[arm] = parsed
            if parsed["record"]["duration_us"] > receipt["duration_us"]:
                reasons.append(arm + " preparation duration exceeds the entire qualification attempt")
    if len(arms) != 2:
        return reasons
    total_preparation_us = 0
    for arm_name in ("legacy", "snapshot"):
        pointer = receipt["preparation_costs"].get(arm_name)
        cost = arms[arm_name]["cost"]
        if not isinstance(pointer, dict) or set(pointer) != {
                "receipt_sha256", "receipt_publication_us", "total_us", "complete_cost_available"} or \
                pointer.get("receipt_sha256") != sha(arms[arm_name]["cost_raw"]) or \
                pointer.get("complete_cost_available") is not True or \
                not uint(pointer.get("receipt_publication_us"), TIME_LIMIT_US) or \
                not uint(pointer.get("total_us"), TIME_LIMIT_US) or \
                pointer["total_us"] != cost["total_us"] + pointer["receipt_publication_us"]:
            reasons.append(arm_name + " qualification whole-operation cost pointer/hash/publication/sum malformed")
        else:
            total_preparation_us += pointer["total_us"]
    if total_preparation_us > receipt["duration_us"]:
        reasons.append("preparation whole-operation costs exceed the entire qualification observation")
    left, right = arms["legacy"]["record"], arms["snapshot"]["record"]
    legacy_stages, snapshot_stages = arms["legacy"]["stages"], arms["snapshot"]["stages"]
    if legacy_stages[0]["finish"] > snapshot_stages[0]["start"] or \
            snapshot_stages[0]["finish"] > legacy_stages[1]["start"] or \
            legacy_stages[-1]["finish"] > snapshot_stages[1]["start"] or \
            receipt["duration_us"] < snapshot_stages[-1]["finish"] - legacy_stages[0]["start"]:
        reasons.append("common ROOT phases overlapped or either arm mutated ROOT before both pins were validated")
    matched_keys = ("root_sha256", "frozen_workload_sha256", "bootstrap_configuration", "bootstrap_artifact_sha256",
                    "harness_sha256", "harness_bytes", "harness_mode", "candidate_sha256", "candidate_bytes", "candidate_mode",
                    "baseline_cache_sha256", "candidate_cache_sha256")
    if any(left[key] != right[key] for key in matched_keys) or arms["legacy"]["workload"] != arms["snapshot"]["workload"]:
        reasons.append("legacy/snapshot matched-root frozen workload/configuration/harness/candidate identity differs")
    for arm, name, same_source in SERIES:
        row = bundles[arm].get(name)
        row = row if isinstance(row, dict) else {}
        baseline_arm = "legacy" if name == "cross-build-aa" else arm
        baseline = frozen_binary(arms[baseline_arm]["record"], baseline_arm, "baseline", expected)
        candidate = frozen_binary(arms[arm]["record"], arm, "baseline" if same_source else "candidate", expected)
        pair = baseline, candidate
        declared = {"baseline": {"sha256": baseline[1]}, "candidate": {"sha256": candidate[1]}}
        label = arm + "/" + name + ": "
        reasons.extend(label + item for item in lab_reasons(row, expected, pair, same_source))
        reasons.extend(label + item for item in classify(row.get("summary"), declared))
        reasons.extend(label + item for item in classify_throughput(row.get("throughput"), row.get("metadata"), declared))
        metadata = row.get("metadata") if isinstance(row.get("metadata"), dict) else {}
        provenance = metadata.get("compiler_provenance")
        wanted_labels = (expected["base"], expected["base" if same_source else "head"])
        if not isinstance(provenance, list) or len(provenance) != 2 or \
                any(not isinstance(item, dict) or item.get("revision_label") != revision
                    for item, revision in zip(provenance, wanted_labels)):
            reasons.append(label + "corpus source revision label population changed")
        if same_source:
            corpus = row.get("throughput") if isinstance(row.get("throughput"), dict) else {}
            if not exact(corpus.get("confirmed_regressions"), 0):
                reasons.append(label + "same-source corpus A/A has confirmed regressions")
        files = arms[arm]["files"]
        for stage in arms[arm]["stages"]:
            if stage["phase"] in {name + suffix for suffix in
                    ("-lab-binaries-before", "-lab-binaries-after", "-throughput-binaries-before", "-throughput-binaries-after")}:
                filename = f"{stage['stage']}-{stage['phase']}.binaries.tsv"
                reasons.extend(label + item for item in binary_check_reasons(files.get(filename), pair))
    return reasons


def validate_prepared(expected: dict, prepared: object, bundle: object) -> list[str]:
    """Public preparation-only replay, shared by qualification and acquisition.

    expected: four Git pins, root/output absolute paths, policy, optional
    secondary_head/secondary_tree and arm_count=3. output is native prepare OUT.
    bundle: manifest/workload/ledger bytes, immediate-basename files byte map,
    and snapshot closure records plus closure_manifests. files must include the\n    complete, raw preparation-cost.json bound to prepared.json. No measured outcomes.
    """
    if not isinstance(expected, dict) or \
            any(not isinstance(expected.get(key), str) or not COMMIT.fullmatch(expected[key]) for key in IDENTITY) or \
            any(not absolute(expected.get(key)) for key in ("root", "output")) or \
            expected.get("policy") not in ("legacy-rebuild", "snapshot-v1"):
        return ["trusted native preparation source/root/output/policy pins missing or malformed"]
    if overlaps(expected["root"], expected["output"]):
        return ["trusted preparation ROOT/output directories overlap"]
    arm_count = expected.get("arm_count", 3 if "secondary_head" in expected or "secondary_tree" in expected else 2)
    if not exact(arm_count, 2) and not exact(arm_count, 3):
        return ["trusted native preparation arm count unsupported"]
    if arm_count == 3 and (expected["policy"] != "snapshot-v1" or any(not isinstance(expected.get(key), str) or
            not COMMIT.fullmatch(expected[key]) for key in ("secondary_head", "secondary_tree"))):
        return ["trusted native preparation secondary pins missing or unsupported"]
    if arm_count == 2 and any(expected.get(key) not in (None, "") for key in ("secondary_head", "secondary_tree")):
        return ["two-arm native preparation unexpectedly declares secondary pins"]
    if not isinstance(prepared, dict) or not isinstance(bundle, dict) or \
            ("prepared" in bundle and bundle["prepared"] != prepared):
        return ["native preparation receipt/bundle missing or inconsistent"]
    arm = "legacy" if expected["policy"] == "legacy-rebuild" else "snapshot"
    errors, _ = preparation_reasons(expected, arm, dict(bundle, prepared=prepared),
                                    preparation_phases(arm, arm_count), qualification=False, arm_count=arm_count)
    return errors
