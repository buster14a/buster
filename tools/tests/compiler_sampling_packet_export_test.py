#!/usr/bin/env python3
"""Hosted-only native packet export and trusted consumer regression fixture.

Run: python3 -B tools/tests/compiler_sampling_packet_export_test.py DRIVER OUTPUT
This invokes the bounded C self-test and packages its actual retained data.
All identities/API lineage are predeclared synthetic fixtures, not authority
for real performance evidence. Native observations alone fill current clocks.
No publication, benchmark request, compiler build or profile activation occurs.
"""
from __future__ import annotations

import copy
import hashlib
import io
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bench_direct"))
import compiler_publish as publisher
import sampling_qualification_receipt as receipt

HOST = re.compile(r"Ryzen\s+7\s+9700X\b", re.I)
MANAGER_SCHEMA = "buster-hosted-packet-fixture-manager-v1"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def refuse_actual_host() -> None:
    check(sys.platform == "linux", "hosted packet fixture requires Linux")
    with open("/proc/cpuinfo", "rb") as handle:
        cpu = handle.read(65537)
    check(bool(cpu) and len(cpu) <= 65536 and not HOST.search(cpu.decode("ascii", errors="strict")),
          "hosted diagnostic fixture refuses the actual Ryzen 7 9700X")


def read_regular(path: Path, limit: int) -> bytes:
    check(path == path.resolve(strict=True), "fixture data path is not canonical")
    before = path.stat(follow_symlinks=False)
    check(stat.S_ISREG(before.st_mode) and 0 <= before.st_size <= limit,
          "fixture data is not a bounded regular file")
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        observed = os.fstat(descriptor)
        with os.fdopen(descriptor, "rb", closefd=False) as handle:
            data = handle.read(limit + 1)
        after = os.fstat(descriptor)
        signature = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)
        check(signature(before) == signature(observed) == signature(after) and len(data) == before.st_size,
              "fixture data changed while read")
    finally:
        os.close(descriptor)
    return data


def json_object(data: bytes) -> dict:
    def unique(items):
        value = {}
        for key, item in items:
            if key in value:
                raise ValueError("duplicate fixture JSON field")
            value[key] = item
        return value
    value = json.loads(data.decode("utf-8"), object_pairs_hook=unique)
    check(isinstance(value, dict), "fixture JSON root must be an object")
    return value


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, allow_nan=False, separators=(",", ":")) + "\n").encode("utf-8")


def native_members(directory: Path) -> dict[str, bytes]:
    result = {}
    total = 0
    check(directory == directory.resolve(strict=True) and directory.is_dir(), "native export root is not canonical")
    for root, directories, files in os.walk(directory, followlinks=False):
        for name in directories:
            child = Path(root) / name
            check(stat.S_ISDIR(child.stat(follow_symlinks=False).st_mode) and not child.is_symlink(),
                  "native export contains a linked or nonregular directory")
        for name in files:
            child = Path(root) / name
            member = child.relative_to(directory).as_posix()
            check(len(result) < 2048, "native export exceeds bounded archive member count")
            data = read_regular(child, publisher.ANALYZER_MEMBER_LIMIT)
            total += len(data)
            check(total <= publisher.ARTIFACT_LIMIT, "native export exceeds archive size bound")
            result[member] = data
    check(bool(result), "native export is empty")
    return result


def stored_zip(members: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
        for name, data in sorted(members.items()):
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_STORED
            archive.writestr(info, data)
    return buffer.getvalue()


def read_export(members: dict[str, bytes], trusted: dict) -> dict:
    # Unpack only the map already bounded and path-checked by the real publisher.
    bounded = publisher.sampling_archive(stored_zip(members))
    check(bounded == members, "publisher did not preserve every native archive member byte")
    with tempfile.TemporaryDirectory(prefix="buster-hosted-packet-consumer-") as temporary:
        directory = Path(temporary)
        for name, data in bounded.items():
            target = directory / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as handle:
                handle.write(data)
        result = receipt.read_packet(directory, copy.deepcopy(trusted))
    return result


def assert_unqualified(result: dict, complete: bool) -> None:
    check(result.get("qualification_state") == "unqualified" and
          result.get("routine_profile_enabled") is False,
          "hosted fixture acquired an unauthorized qualification/activation label")
    wanted = "complete-valid-research" if complete else "incomplete"
    check(result.get("packet_state") == wanted, "unexpected packet state: " + repr(result))
    if not complete:
        check(bool(result.get("problems")), "negative fixture did not retain a failure reason")


def trusted_observations(members: dict[str, bytes], manager_bytes: bytes) -> dict:
    expected = json_object(members["fixture-expected.json"])
    check(expected.get("diagnostic_fixture") is True and expected.get("actual_approved_host") is False,
          "expected facts must explicitly declare their synthetic diagnostic status")
    check(isinstance(expected.get("trusted"), dict), "pre-outcome trusted fixture facts are absent")
    trusted = copy.deepcopy(expected["trusted"])
    check(trusted.get("authenticated") is True and
          set(trusted) == {"authenticated", "request", "executor", "identity", "binaries", "workload_config", "attempts"},
          "pre-outcome fixture fact structure is not the declared consumer contract")
    request, executor, identity = trusted["request"], trusted["executor"], trusted["identity"]
    history = trusted["attempts"]
    check(request.get("phase") == "pilot" and request.get("packet") == 0 and identity.get("family") == "aa" and
          identity.get("base") == "a" * 40 and identity.get("base_tree") == "b" * 40 and
          identity.get("request_head") == "c" * 40 and identity.get("trusted_revision") == "d" * 40 and
          request.get("freeze_revision") == "d" * 40, "unexpected predeclared synthetic packet/source pins")
    check(isinstance(history, list) and len(history) == 2 and
          [(row.get("phase"), row.get("packet")) for row in history] == [("acquire", 0), ("pilot", 0)],
          "fixture attempt lineage must be predeclared acquisition then pilot-zero")
    check(executor.get("physical_packet_wall_us") is None and executor.get("actions_job_occupancy_us") is None and
          history[-1].get("physical_packet_wall_us") is None and history[-1].get("actions_job_occupancy_us") is None,
          "pre-outcome fixture expectations must not manufacture current measured clocks")
    manager = publisher.sampling_tsv(manager_bytes)
    check(manager.get("schema") == MANAGER_SCHEMA and manager.get("state") == "complete" and
          manager.get("actual_approved_host") == "false" and manager.get("owned_worker") == "true" and
          manager.get("cleanup_failed") == "0", "native fixture manager did not prove hosted owned-worker completion")
    packet = publisher.sampling_tsv(members["packet.tsv"])
    physical = receipt.integer(packet.get("physical_packet_wall_us"))
    job = receipt.integer(manager.get("job_wall_us"))
    worker = receipt.integer(manager.get("worker_wall_us"))
    check(physical is not None and physical > 0 and job is not None and job >= physical and
          worker is not None and 0 < worker <= job, "native manager/job clocks do not cover actual packet occupancy")
    executor["physical_packet_wall_us"] = physical
    executor["actions_job_occupancy_us"] = job
    history[-1]["physical_packet_wall_us"] = physical
    history[-1]["actions_job_occupancy_us"] = job
    return trusted


def negative_cases(original: dict[str, bytes], trusted: dict) -> int:
    payload = stored_zip(original)
    rejected = False
    try:
        publisher.sampling_archive(payload[:-16])
    except (ValueError, OSError, zipfile.BadZipFile):
        rejected = True
    check(rejected, "truncated ZIP archive was accepted")
    changed = dict(original)
    changed["trial-1/pairs.json"] = changed["trial-1/pairs.json"][:-4]
    assert_unqualified(read_export(changed, trusted), False)
    changed = dict(original)
    raw = json_object(changed["trial-1/compare.json"])
    raw["config"]["cpu"] = 0
    changed["trial-1/compare.json"] = json_bytes(raw)
    assert_unqualified(read_export(changed, trusted), False)
    changed = dict(original)
    raw = json_object(changed["trial-1/compare.json"])
    raw["steps"]["timed"]["status"] = "failed"
    changed["trial-1/compare.json"] = json_bytes(raw)
    assert_unqualified(read_export(changed, trusted), False)
    changed = dict(original)
    rows = publisher.sampling_tsv(changed["attempts.tsv"], table=True)
    rows[1]["closure_after"] = "0" * 64
    names = list(rows[0])
    changed["attempts.tsv"] = ("\t".join(names) + "\n" +
        "".join("\t".join(row[name] for name in names) + "\n" for row in rows)).encode("ascii")
    assert_unqualified(read_export(changed, trusted), False)
    return 5


def main() -> int:
    result = 1
    try:
        refuse_actual_host()
        check(len(sys.argv) == 3, "usage: compiler_sampling_packet_export_test.py DRIVER OUTPUT")
        driver = Path(sys.argv[1]).resolve(strict=True)
        check(stat.S_ISREG(driver.stat().st_mode) and os.access(driver, os.X_OK), "native driver is not executable")
        output = Path(sys.argv[2]).absolute()
        check(not output.exists() and not output.is_symlink() and
              output.parent == output.parent.resolve(strict=True), "fixture output must be fresh under a canonical parent")
        process = subprocess.run([str(driver), "compiler_profile_qualification", "--self-test-packet-export", str(output)],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 timeout=60, check=False)
        check(len(process.stdout) <= 1048576 and len(process.stderr) <= 1048576,
              "native fixture emitted excessive diagnostics")
        if process.returncode:
            sys.stderr.write(process.stdout.decode("utf-8", errors="replace"))
            sys.stderr.write(process.stderr.decode("utf-8", errors="replace"))
        check(process.returncode == 0, "native hosted packet-export fixture failed")
        members = native_members(output / "export")
        expected_sha = hashlib.sha256(members["fixture-expected.json"]).hexdigest()
        manager = read_regular(output / "fixture-manager.tsv", 16384)
        trusted = trusted_observations(members, manager)
        for index in range(3):
            for name in ("a/metadata.json", "a/commands.log", "b/metadata.json", "b/commands.log", "pairs/0001-a.csv"):
                check("trial-%d/%s" % (index, name) in members, "native archive lost nested raw fixture evidence")
        for name in ("identity.tsv", "attempts.tsv", "packet.tsv", "claim.tsv", "owner.tsv", "owner-supervision.tsv"):
            check(name in members, "native archive omitted required packet/claim evidence")
        claim = publisher.sampling_tsv(members["claim.tsv"])
        check(claim.get("schema") == "buster-main-sampling-reservation-v1" and
              claim.get("phase") == "pilot" and claim.get("packet") == "0" and claim.get("family") == "aa" and
              claim.get("campaign") == trusted["identity"]["campaign"] and
              claim.get("reservation_seconds") == "3000" and claim.get("retry_allowed") == "false",
              "native claim differs from the predeclared non-retryable packet")
        publisher.sampling_supervision(members["owner-supervision.tsv"])
        for index in range(3):
            for name in ("trial-%d-supervision.tsv" % index,
                         "closure-%d-before-supervision.tsv" % index,
                         "closure-%d-after-supervision.tsv" % index):
                check(name in members, "native archive omitted actual process supervision")
                publisher.sampling_supervision(members[name])
        assert_unqualified(read_export(members, trusted), True)
        cases = negative_cases(members, trusted)
        check(hashlib.sha256(read_regular(output / "export" / "fixture-expected.json", 16384)).hexdigest() == expected_sha,
              "predeclared expected facts changed during consumer checks")
        print("HOSTED_PACKET_EXPORT_TEST positive=1 negatives=%d members=%d diagnostic_fixture=true "
              "actual_approved_host=false qualification_state=unqualified status=pass" % (cases, len(members)))
        result = 0
    except (OSError, ValueError, TypeError, KeyError, IndexError, subprocess.TimeoutExpired) as error:
        print("hosted packet export fixture rejected: " + str(error), file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
