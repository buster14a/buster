#!/usr/bin/env python3
"""Offline per-activation #1162 broker entry consumer (selected criterion).

#1162 comment 5858483525 selects mandatory same-PID pre-exec enforcement plus
complete per-instance evidence instead of requiring an external /proc snapshot
to win every millisecond race. For EVERY activated broker instance this
consumer requires one exact join of:

* PID 1's own journal: one Starting/Started start job and one terminal record
  (success, or the exact exit-code failure pair) for the same invocation;
* the independent D-Bus observer: one lifecycle generation whose manager
  signals report the same MainPID, then removal (success) or a completed
  retained-failed `systemctl show` naming the same invocation and PID;
* exactly one `BQ-BROKER-ENTRY-V1 PASS` gate record whose trusted journald
  PID/unit/invocation/stream equal the broker's, whose start ticks and FD0
  socket equal the broker diagnostic, which precedes the broker's first
  record, and whose installed file tuples, hashes and accounts equal the
  independent installation readback taken before and after the run;
* one complete broker BEGIN/DATA/SNAPSHOT_END/REQUEST/OUTCOME sequence whose
  request is allowlisted and whose outcome agrees with PID 1's terminal.

Missing, duplicate or inconsistent evidence fails the whole attempt. External
process captures remain retained; every complete one must agree with the
joined identity, but their absence is reported rather than required.
Reuses the trusted-journal, diagnostic, request, profile and observer document
validators in issue1162_broker_evidence.py.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import issue1162_broker_evidence as base  # noqa: E402

EvidenceError = base.EvidenceError
require = base.require

ENTRY_SCHEMA = "issue1162-broker-entry-expectation-v1"
READBACK_SCHEMA = "issue1162-broker-entry-readback-v1"
RESULT_SCHEMA = "issue1162-broker-entry-result-v1"
GATE_PATH = "/usr/local/libexec/buster-bench-broker-entry-gate"
BROKER_PATH = "/usr/local/libexec/buster-bench-systemd-broker"
READBACK_PATHS = {
    "gate": GATE_PATH,
    "broker": BROKER_PATH,
    "receipt": "/etc/buster-bench/systemd-broker-accounts.identity",
    "passwd": "/etc/passwd",
    "group": "/etc/group",
    "nsswitch": "/etc/nsswitch.conf",
}
READBACK_MODES = {"gate": 0o100755, "broker": 0o100755, "receipt": 0o100444}
UNIT_SUCCESS_MESSAGE_ID = "7ad2d189f7e94e70a38c781354912448"
ENTRY_MARKER = "BQ-BROKER-ENTRY-V1"
# systemd names a unit's journal stream after its ExecStart file, not after
# anything the process writes; every PASS and broker record must carry it,
# which independently shows the gate was the unit's executable.
STREAM_IDENTIFIER = "buster-bench-broker-entry-gate"
MANAGER_INTERFACE = "org.freedesktop.systemd1.Manager"
PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
_N = r"(0|[1-9][0-9]*)"
_S = r"(0|-?[1-9][0-9]*)"
_TUPLE = rf"{_N}:{_N}:{_S}:{_S}:{_N}:{_S}:{_N}"
_TUPLE_RE = re.compile(rf"{_TUPLE}\Z")
PASS_RE = re.compile(
    rf"BQ-BROKER-ENTRY-V1 PASS boot=([0-9a-f]{{8}}-[0-9a-f]{{4}}-[0-9a-f]{{4}}-[0-9a-f]{{4}}-[0-9a-f]{{12}}) "
    rf"pid=([1-9][0-9]*) ticks=([1-9][0-9]*) invocation=([0-9a-f]{{32}}) cgroup=(/[^ ]+) "
    rf"socket={_N}:{_N} gate=({_TUPLE}) broker=({_TUPLE}) receipt=({_TUPLE}) "
    rf"accounts={_N},{_N},{_N},{_N},{_N},{_N} "
    rf"passwd=({_TUPLE}) group=({_TUPLE}) nsswitch=({_TUPLE}) "
    r"passwd-sha256=([0-9a-f]{64}) group-sha256=([0-9a-f]{64}) nsswitch-sha256=([0-9a-f]{64}) "
    rf"uid=0 gid={_N} groups={_N},{_N} caps=0 nnp=1 seccomp=2 mounts=ro fd0=seqpacket\Z")


def _canonical_tuple(text: str, label: str) -> str:
    require(isinstance(text, str) and _TUPLE_RE.fullmatch(text) is not None,
            f"{label} is not a canonical dev:ino:size:mtime:mtime_ns:ctime:ctime_ns tuple")
    return text


def parse_pass(message: str) -> dict[str, Any]:
    """Parse one exact gate PASS line; the grammar has no optional fields.

    The `uid/gid/groups/caps/nnp/seccomp/mounts/fd0` tokens are the gate's
    fixed assertion, printed only after its checks passed; they are not
    measured values. Credentials are enforced by the gate before PASS and
    independently read from `/proc/self/status` in the broker diagnostic.
    """
    require(isinstance(message, str) and len(message) < 4096 and "\n" not in message,
            "gate PASS is not one bounded text line")
    match = PASS_RE.fullmatch(message)
    require(match is not None, "malformed BQ-BROKER-ENTRY-V1 PASS record")
    groups = match.groups()
    # Group layout follows PASS_RE; tuple groups each add seven inner groups.
    boot, pid, ticks, invocation, cgroup, sock_dev, sock_ino = groups[:7]
    at = 7
    tuples: dict[str, str] = {}
    for name in ("gate", "broker", "receipt"):
        tuples[name] = groups[at]
        at += 8
    accounts = [int(value) for value in groups[at:at + 6]]
    at += 6
    for name in ("passwd", "group", "nsswitch"):
        tuples[name] = groups[at]
        at += 8
    hashes = {"passwd": groups[at], "group": groups[at + 1], "nsswitch": groups[at + 2]}
    at += 3
    gid, group_a, group_b = (int(value) for value in groups[at:at + 3])
    return {"boot_id_raw": boot, "pid": int(pid), "ticks": int(ticks), "invocation": invocation,
            "cgroup": cgroup, "socket": (int(sock_dev), int(sock_ino)), "tuples": tuples,
            "accounts": accounts, "hashes": hashes, "gid": gid, "groups": (group_a, group_b)}


def collect_passes(records: list[dict[str, Any]], expected_boot: str) -> dict[tuple[str, str, str, int], dict[str, Any]]:
    passes: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    for number, record in enumerate(records, 1):
        message = record.get("MESSAGE")
        if isinstance(message, list):
            # journalctl renders non-UTF-8 or binary messages as byte arrays.
            try:
                text = bytes(message).decode("latin-1")
            except (TypeError, ValueError):
                text = ENTRY_MARKER
        else:
            text = message if isinstance(message, str) else json.dumps(message)
        if ENTRY_MARKER not in text:
            continue
        require(isinstance(message, str) and message.startswith(ENTRY_MARKER + " PASS "),
                f"journal record {number} embeds the entry marker outside an exact PASS line")
        require("_LINE_BREAK" not in record, f"gate PASS record {number} was split by journald")
        require(base._field_text(record, "_TRANSPORT", "gate transport") == "stdout",
                f"gate PASS record {number} is not a unit stream record")
        require(record.get("SYSLOG_IDENTIFIER") == STREAM_IDENTIFIER,
                f"gate PASS record {number} is not on the entry gate's journal stream")
        boot = base._field_text(record, "_BOOT_ID", "gate boot ID")
        require(boot == expected_boot, f"gate PASS record {number} has the wrong boot")
        unit = base._field_text(record, "_SYSTEMD_UNIT", "gate unit")
        base.parse_broker_unit(unit, f"gate PASS record {number} unit")
        invocation = base._field_text(record, "_SYSTEMD_INVOCATION_ID", "gate invocation")
        require(base.HEX32.fullmatch(invocation) is not None and invocation != "0" * 32,
                f"gate PASS record {number} has a malformed invocation")
        pid = base.canonical_uint(base._field_text(record, "_PID", "gate PID"),
                                  "trusted gate PID", (1 << 31) - 1, False)
        stream = base._field_text(record, "_STREAM_ID", "gate stream")
        require(base.HEX32.fullmatch(stream) is not None, f"gate PASS record {number} has a malformed stream")
        parsed = parse_pass(message)
        require(parsed["pid"] == pid and parsed["invocation"] == invocation,
                f"gate PASS record {number} self-report differs from trusted journald identity")
        key = (boot, unit, invocation, pid)
        require(key not in passes, f"duplicate gate PASS for {unit}")
        require(len(passes) < base.MAX_INSTANCES, "gate PASS population exceeds bound")
        parsed.update({"record_number": number, "stream_id": stream, "unit": unit})
        passes[key] = parsed
    return passes


def collect_success_terminals(records: list[dict[str, Any]], expected_boot: str) -> dict[tuple[str, str, str], int]:
    rows: dict[tuple[str, str, str], int] = {}
    for number, record in enumerate(records, 1):
        unit = record.get("UNIT")
        if (record.get("MESSAGE_ID") != UNIT_SUCCESS_MESSAGE_ID or not isinstance(unit, str) or
                not unit.startswith("buster-bench-systemd-broker@")):
            continue
        base.parse_broker_unit(unit, f"PID1 success record {number} unit")
        require(record.get("_PID") == "1" and record.get("_TRANSPORT") == "journal" and
                record.get("_BOOT_ID") == expected_boot,
                f"broker success record {number} is not from the exact PID1/boot journal")
        invocation = base._field_text(record, "INVOCATION_ID", "PID1 success invocation")
        require(base.HEX32.fullmatch(invocation) is not None and invocation != "0" * 32,
                "PID1 success invocation is malformed")
        key = (expected_boot, unit, invocation)
        require(key not in rows, f"duplicate PID1 success record for {unit}")
        rows[key] = number
    return rows


def parse_readback(raw: bytes, label: str) -> dict[str, dict[str, Any]]:
    value = base.strict_json(raw, label)
    require(isinstance(value, dict) and value.get("schema") == READBACK_SCHEMA and
            set(value) == {"schema", "files"} and isinstance(value["files"], dict) and
            set(value["files"]) == set(READBACK_PATHS), f"{label} schema or file set differs")
    files: dict[str, dict[str, Any]] = {}
    for name, path in READBACK_PATHS.items():
        row = value["files"][name]
        require(isinstance(row, dict) and
                set(row) == {"path", "tuple", "sha256", "uid", "gid", "mode", "nlink", "bytes"} and
                row["path"] == path, f"{label} {name} row differs")
        _canonical_tuple(row["tuple"], f"{label} {name} tuple")
        require(isinstance(row["sha256"], str) and base.HEX64.fullmatch(row["sha256"]) is not None and
                type(row["uid"]) is int and type(row["gid"]) is int and type(row["mode"]) is int and
                type(row["nlink"]) is int and type(row["bytes"]) is int,
                f"{label} {name} fields are malformed")
        require(row["uid"] == 0 and row["nlink"] == 1 and (row["mode"] & 0o170000) == 0o100000 and
                not row["mode"] & 0o7022, f"{label} {name} is not a root-owned single-link nonwritable file")
        if name in READBACK_MODES:
            require(row["mode"] == READBACK_MODES[name], f"{label} {name} mode differs from installation")
        require(int(row["tuple"].split(":")[2]) == row["bytes"], f"{label} {name} tuple size differs")
        files[name] = row
    return files


def parse_entry_expected(raw: bytes) -> dict[str, Any]:
    value = base.strict_json(raw, "entry expectation")
    require(isinstance(value, dict) and value.get("schema") == ENTRY_SCHEMA and
            set(value) == {"schema", "gate_sha256", "broker_sha256", "accounts", "readbacks"},
            "entry expectation schema differs")
    for key in ("gate_sha256", "broker_sha256"):
        require(isinstance(value[key], str) and base.HEX64.fullmatch(value[key]) is not None,
                f"entry expectation {key} is malformed")
    require(isinstance(value["accounts"], list) and len(value["accounts"]) == 6 and
            all(type(item) is int and 0 < item < (1 << 32) - 1 for item in value["accounts"]),
            "entry expectation accounts are malformed")
    require(value["accounts"] == [65000, 65000, 65001, 65001, 65002, 65002],
            "entry expectation accounts differ from the fixed installation receipt")
    readbacks = value["readbacks"]
    require(isinstance(readbacks, list) and len(readbacks) == 2 and all(isinstance(item, str) for item in readbacks),
            "entry expectation must name the before and after readbacks")
    return value


def load_readbacks(expected: dict[str, Any]) -> dict[str, dict[str, Any]]:
    loaded = []
    for index, path in enumerate(expected["readbacks"]):
        require(path.startswith("/") and ".." not in PurePosixPath(path).parts,
                "readback path must be absolute and normalized")
        loaded.append(parse_readback(base.read_absolute_file(path, 1024 * 1024), f"readback {index}"))
    require(loaded[0] == loaded[1], "installed gate, broker, receipt or account sources changed during the run")
    files = loaded[0]
    require(files["gate"]["sha256"] == expected["gate_sha256"] and
            files["broker"]["sha256"] == expected["broker_sha256"],
            "installed gate or broker bytes differ from the reviewed payload")
    return files


def join_activations(passes: dict[tuple[str, str, str, int], dict[str, Any]],
                     diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
                     profiles: dict[tuple[str, str, str, int], dict[str, Any]],
                     manager_rows: dict[tuple[str, str], dict[str, Any]],
                     failed_rows: dict[tuple[str, str, str], dict[str, Any]],
                     success_rows: dict[tuple[str, str, str], int],
                     observer_units: dict[str, dict[str, Any]],
                     readback: dict[str, dict[str, Any]],
                     expected: dict[str, Any], entry: dict[str, Any]) -> list[dict[str, Any]]:
    """Require one exact manager/observer/gate/broker/outcome row per activation."""
    require(bool(passes), "no gate PASS was recorded")
    require(set(passes) == set(diagnostics),
            "gate PASS and broker diagnostic activations differ")
    units = [key[1] for key in passes]
    require(len(units) == len(set(units)), "one broker unit name has several activations")
    require({key[1] for key in manager_rows} == set(units) and
            all(key[0] == expected["boot_id"] for key in manager_rows),
            "PID 1 start jobs and gate PASS activations differ")
    require(set(observer_units) == set(units), "observer lifecycles and gate PASS activations differ")
    terminal_units = [key[1] for key in failed_rows] + [key[1] for key in success_rows]
    require(sorted(terminal_units) == sorted(units), "PID 1 terminal records and activations differ")
    prefix = expected["profile"]["cgroup_prefix"]
    rows = []
    for key in sorted(passes, key=lambda item: passes[item]["record_number"]):
        boot, unit, invocation, pid = key
        record = passes[key]
        diagnostic = diagnostics[key]
        sequence = diagnostic["sequence"]
        profile = profiles[key]
        manager = manager_rows[(boot, unit)]
        require(manager["result"] == "done" and manager["invocation_id"] == invocation,
                f"PID 1 start job did not start exactly invocation {invocation} of {unit}")
        require(record["boot_id_raw"] == expected["boot_id_raw"],
                f"gate PASS boot differs for {unit}")
        require(record["ticks"] == sequence["start_ticks"],
                f"gate PASS and broker start ticks differ: not the same process start for {unit}")
        require(record["stream_id"] == diagnostic["stream_id"],
                f"gate PASS and broker diagnostic use different journal streams for {unit}")
        require(diagnostic.get("identifiers") == {STREAM_IDENTIFIER},
                f"broker records for {unit} are not on the entry gate's journal stream")
        require(record["record_number"] < min(diagnostic["record_numbers"]),
                f"gate PASS does not precede the broker's first record for {unit}")
        socket = sequence["socket"]
        require(record["socket"] == (socket["dev"], socket["ino"]),
                f"gate PASS FD0 differs from the broker's FD0 for {unit}")
        require(record["cgroup"] == prefix + unit == profile["cgroup_path"],
                f"gate PASS cgroup differs from the exact broker unit cgroup for {unit}")
        require(record["accounts"] == entry["accounts"] and record["gid"] == entry["accounts"][1] and
                sorted(record["groups"]) == [entry["accounts"][1], entry["accounts"][3]],
                f"gate PASS accounts or credentials differ from the installation receipt for {unit}")
        for name in ("gate", "broker", "receipt", "passwd", "group", "nsswitch"):
            require(record["tuples"][name] == readback[name]["tuple"],
                    f"gate PASS {name} identity differs from the installation readback for {unit}")
        for name in ("passwd", "group", "nsswitch"):
            require(record["hashes"][name] == readback[name]["sha256"],
                    f"gate PASS {name} bytes differ from the installation readback for {unit}")
        require(sequence["exe"]["dev"] == int(readback["broker"]["tuple"].split(":")[0]) and
                sequence["exe"]["ino"] == int(readback["broker"]["tuple"].split(":")[1]) and
                sequence["exe"]["path"] == BROKER_PATH,
                f"broker executed from a file other than the installed broker for {unit}")
        outcome = sequence["outcome"]
        failure = failed_rows.get((boot, unit, invocation))
        succeeded = (boot, unit, invocation) in success_rows
        require(succeeded != (failure is not None), f"PID 1 terminal for {unit} is missing or ambiguous")
        if succeeded:
            require(outcome["broker_exit"] == 0, f"PID 1 success disagrees with broker exit for {unit}")
        else:
            require(outcome["broker_exit"] == 1 and failure["exit"]["code"] == "exited" and
                    failure["exit"]["status"] == "1" and failure["failure"]["result"] == "exit-code",
                    f"PID 1 failure disagrees with broker exit for {unit}")
        observed = observer_units[unit]
        require(observed["main_pids"] == {pid},
                f"observer manager signals report a MainPID other than the gated broker for {unit}")
        if succeeded:
            require(observed["removed"] == 1 and observed["retained"] is None,
                    f"successful broker lifecycle was not removed exactly once for {unit}")
        else:
            retained = observed["retained"]
            require(observed["removed"] == 0 and retained is not None and
                    retained["InvocationID"] == invocation and retained["ExecMainPID"] == str(pid),
                    f"retained failed lifecycle does not name the gated invocation and PID for {unit}")
            exit_us = int(retained["ExecMainExitTimestampMonotonic"])
            require(exit_us <= failure["exit"]["monotonic_us"] <= failure["failure"]["monotonic_us"] and
                    failure["failure"]["monotonic_us"] * 1000 <= int(retained["query_started_monotonic_ns"]),
                    f"retained failed show, process exit and PID 1 failure are out of order for {unit}")
        rows.append({"unit": unit, "invocation_id": invocation, "pid": pid, "start_ticks": record["ticks"],
                     "socket_inode": socket["ino"], "gate_record": record["record_number"],
                     "first_broker_record": min(diagnostic["record_numbers"]),
                     "outcome": "success" if succeeded else "exit-code",
                     "request_known": bool(sequence["request"]["request_known"]),
                     "peer_uid": sequence["request"]["peer_uid"]})
    return rows


RETAINED_KEYS = ("Id", "InvocationID", "ExecMainPID", "ExecMainCode", "ExecMainStatus",
                 "Result", "ActiveState", "SubState", "LoadState",
                 "ExecMainStartTimestampMonotonic", "ExecMainExitTimestampMonotonic")


def _retained_properties(observer_fd: int, unit: str, terminal: Any) -> dict[str, str]:
    require(isinstance(terminal, dict) and terminal.get("unit") == unit and
            terminal.get("systemctl_exit") == 0 and terminal.get("systemctl_timed_out") is False and
            terminal.get("systemctl_cancelled") is None,
            f"retained failed query did not complete for {unit}")
    ref = terminal.get("raw_show")
    require(isinstance(ref, dict) and ref.get("exit") == 0 and ref.get("timed_out") is False,
            f"retained failed raw show is incomplete for {unit}")
    raw = base._safe_ref(observer_fd, ref, "retained failed show", 256 * 1024)
    stderr = base._safe_ref(observer_fd, terminal.get("raw_stderr"), "retained failed stderr", 32 * 1024)
    require(not stderr, f"retained failed query wrote diagnostics for {unit}")
    started_ns = terminal.get("started_monotonic_ns")
    require(type(started_ns) is int and started_ns > 0, f"retained failed query has no start time for {unit}")
    properties: dict[str, str] = {}
    for line in raw.decode("utf-8", "strict").splitlines():
        name, sep, value = line.partition("=")
        if sep and name in RETAINED_KEYS:
            require(name not in properties, f"retained show repeats {name} for {unit}")
            properties[name] = value
    fixed = {"Id": unit, "ExecMainCode": "1", "ExecMainStatus": "1", "Result": "exit-code",
             "ActiveState": "failed", "SubState": "failed", "LoadState": "loaded"}
    require(set(properties) == set(RETAINED_KEYS) and
            all(properties[key] == value for key, value in fixed.items()),
            f"retained failed show does not describe the exact failed broker for {unit}")
    start_us = base.canonical_uint(properties["ExecMainStartTimestampMonotonic"], "retained start", (1 << 64) - 1, False)
    exit_us = base.canonical_uint(properties["ExecMainExitTimestampMonotonic"], "retained exit", (1 << 64) - 1, False)
    require(start_us <= exit_us and exit_us * 1000 <= started_ns,
            f"retained failed show was queried before the exit it reports for {unit}")
    properties["query_started_monotonic_ns"] = str(started_ns)
    return properties


def lifecycles_from_events(events: list[dict[str, Any]], summary: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Exact manager-signal lifecycle per unit; retained terminals are bound later."""
    require(summary.get("event_stream_complete") is True and summary.get("event_loss_detected") is False and
            summary.get("stop_seen") is True and summary.get("global_incomplete_reasons") == [],
            "observer manager signal stream is incomplete")
    sender = summary.get("manager_sender")
    units: dict[str, dict[str, Any]] = {}
    previous_ns = 0
    for event in events:
        unit = event.get("unit")
        member = event.get("member")
        require(isinstance(unit, str) and member in ("UnitNew", "UnitRemoved", "PropertiesChanged"),
                f"observer event {event.get('seq')} is not a broker lifecycle signal")
        base.parse_broker_unit(unit, f"observer event {event.get('seq')} unit")
        interface = PROPERTIES_INTERFACE if member == "PropertiesChanged" else MANAGER_INTERFACE
        require(event.get("sender") == sender and event.get("interface") == interface and
                event.get("object_path") == base._unit_object_path(unit),
                f"observer event {event.get('seq')} is not the manager's signal for {unit}")
        require(type(event.get("monotonic_ns")) is int and event["monotonic_ns"] >= previous_ns,
                f"observer event {event.get('seq')} is out of time order")
        previous_ns = event["monotonic_ns"]
        require(event.get("generation") == 1, f"observer saw several lifecycle generations of {unit}")
        row = units.setdefault(unit, {"new": 0, "removed": 0, "main_pids": set(), "retained": None,
                                      "first_seq": event["seq"]})
        if member == "UnitNew":
            require(row["new"] == 0 and event["seq"] == row["first_seq"],
                    f"observer lifecycle of {unit} does not start with exactly one UnitNew")
            row["new"] = 1
        elif member == "UnitRemoved":
            require(row["new"] == 1 and row["removed"] == 0, f"observer saw {unit} removed out of order")
            row["removed"] = 1
        else:
            require(row["new"] == 1 and row["removed"] == 0,
                    f"observer saw {unit} change outside its lifecycle")
            interface_name = event.get("unit_interface")
            require(interface_name in ("org.freedesktop.systemd1.Unit", "org.freedesktop.systemd1.Service"),
                    f"observer property signal for {unit} is not a unit or service interface")
            pids = event.get("main_pids")
            require(isinstance(pids, list) and all(type(value) is int and value >= 0 for value in pids) and
                    (interface_name == "org.freedesktop.systemd1.Service" or not pids),
                    f"observer MainPID values are malformed for {unit}")
            row["main_pids"].update(value for value in pids if value > 0)
    summary_units = summary.get("units")
    require(isinstance(summary_units, list) and
            [row.get("unit") if isinstance(row, dict) else None for row in summary_units].count(None) == 0,
            "observer summary lacks its unit rows")
    names = [row["unit"] for row in summary_units]
    require(len(names) == len(set(names)) and set(names) == set(units),
            "observer summary and signal populations differ or repeat a unit")
    for row in summary_units:
        require(row.get("generation") == 1 and row.get("boot_id") == summary.get("boot_id"),
                f"observer summary row for {row['unit']} is not the single signalled generation")
    for unit, row in units.items():
        require(row["new"] == 1, f"observer lacks UnitNew for {unit}")
    return units


def observer_lifecycles(observer_fd: int, expected: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    ready, events, summary = base._observer_documents(observer_fd, expected)
    base._validate_template_metadata(ready, events, summary)
    units = lifecycles_from_events(events, summary)
    for summary_row in summary["units"]:
        terminal = summary_row.get("retained_failed_terminal")
        if terminal is not None:
            unit = summary_row["unit"]
            units[unit]["retained"] = _retained_properties(observer_fd, unit, terminal)
    for unit, row in units.items():
        require((row["removed"] == 1) != (row["retained"] is not None),
                f"observer lifecycle of {unit} was neither removed nor retained failed")
    return units, summary


def external_consistency(summary: dict[str, Any], rows: list[dict[str, Any]],
                         overlap: list[dict[str, Any]]) -> dict[str, int]:
    """Every retained complete external capture must agree; none is required."""
    identity = {row["unit"]: row for row in rows}
    checked = 0
    for summary_row in summary["units"]:
        joined = identity[summary_row["unit"]]
        for snapshot in summary_row.get("snapshots") or []:
            if not (isinstance(snapshot, dict) and snapshot.get("complete") is True and
                    snapshot.get("process_capture_complete") is True and
                    type(snapshot.get("main_pid")) is int and snapshot["main_pid"] > 0):
                continue
            require(snapshot["main_pid"] == joined["pid"] and
                    snapshot.get("invocation_id") == joined["invocation_id"] and
                    snapshot.get("start_ticks") == joined["start_ticks"],
                    f"complete external capture contradicts the gated identity for {joined['unit']}")
            checked += 1
    for row in overlap:
        require(isinstance(row, dict) and row.get("unit") in identity,
                "live-probe broker capture names a unit outside the activation population")
        joined = identity[row["unit"]]
        require(row.get("pid") == joined["pid"] and row.get("start_ticks") == joined["start_ticks"] and
                row.get("invocation_id") == joined["invocation_id"],
                f"live-probe broker capture contradicts the gated identity for {joined['unit']}")
        checked += 1
    return {"complete_external_captures_checked": checked, "activations": len(rows),
            "activations_with_complete_external_capture": len({
                summary_row["unit"] for summary_row in summary["units"]
                if any(isinstance(s, dict) and s.get("complete") is True and
                       s.get("process_capture_complete") is True for s in summary_row.get("snapshots") or [])})}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--journal-jsonl")
    parser.add_argument("--observer-dir")
    parser.add_argument("--expected-json")
    parser.add_argument("--entry-expected-json")
    parser.add_argument("--overlap-jsonl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not (args.journal_jsonl and args.observer_dir and args.expected_json and args.entry_expected_json):
        parser.error("--journal-jsonl, --observer-dir, --expected-json and --entry-expected-json are required")
    result: dict[str, Any] = {"schema": RESULT_SCHEMA, "entry_complete": False, "activations": None,
                              "external": None, "diagnostic_counts": None, "error": None}
    observer_fd = None
    try:
        expected = base.parse_expected(base.read_absolute_file(args.expected_json, base.MAX_FILE))
        entry = parse_entry_expected(base.read_absolute_file(args.entry_expected_json, 1024 * 1024))
        readback = load_readbacks(entry)
        records = base._validate_journal_capture(base.read_absolute_file(args.journal_jsonl, base.MAX_FILE),
                                                 expected)
        diagnostics = base.collect_diagnostics(records, expected["boot_id"])
        for diagnostic_entry in diagnostics.values():
            diagnostic_entry["identifiers"] = {records[number - 1].get("SYSLOG_IDENTIFIER")
                                               for number in diagnostic_entry["record_numbers"]}
        result["diagnostic_counts"] = base.validate_diagnostic_request_set(diagnostics, expected)
        profiles = base._validate_diagnostic_profiles(diagnostics, expected)
        passes = collect_passes(records, expected["boot_id"])
        manager_rows = base.collect_manager_start_rows(records, expected["boot_id"])
        failed_rows = base.collect_manager_failed_exit_rows(records, expected["boot_id"])
        success_rows = collect_success_terminals(records, expected["boot_id"])
        observer_fd = base.open_absolute_directory(args.observer_dir)
        units, summary = observer_lifecycles(observer_fd, expected)
        rows = join_activations(passes, diagnostics, profiles, manager_rows, failed_rows, success_rows,
                                units, readback, expected, entry)
        overlap: list[dict[str, Any]] = []
        if args.overlap_jsonl:
            raw = base.read_absolute_file(args.overlap_jsonl, base.MAX_OVERLAP, allow_empty=True)
            overlap = base.parse_jsonl(raw, "overlap JSONL") if raw else []
        result["external"] = external_consistency(summary, rows, overlap)
        result["activations"] = rows
        result["entry_complete"] = True
    except (EvidenceError, OSError, ValueError, KeyError, TypeError) as exc:
        result["error"] = (str(exc).replace("\x00", "?")[:512] or type(exc).__name__)
    finally:
        if observer_fd is not None:
            os.close(observer_fd)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["entry_complete"] else 2


def _fixture_tuple(seed: int) -> str:
    return f"48:{9000 + seed}:{1000 + seed}:1790000000:{seed}:1790000001:{seed + 1}"


def _fixture() -> dict[str, Any]:
    boot = "79a44eb01c294793a3c28e143a327a3b"
    boot_raw = "79a44eb0-1c29-4793-a3c2-8e143a327a3b"
    prefix = "/system.slice/system-buster\\x2dbench\\x2dsystemd\\x2dbroker.slice/"
    readback = {name: {"path": path, "tuple": _fixture_tuple(index), "sha256": f"{index:x}" * 64,
                       "uid": 0, "gid": 0, "mode": READBACK_MODES.get(name, 0o100644), "nlink": 1,
                       "bytes": 1000 + index}
                for index, (name, path) in enumerate(READBACK_PATHS.items())}
    for name in readback:
        readback[name]["sha256"] = readback[name]["sha256"][:64]
    entry = {"accounts": [65000, 65000, 65001, 65001, 65002, 65002]}
    expected = {"boot_id": boot, "boot_id_raw": boot_raw, "profile": {"cgroup_prefix": prefix}}
    passes, diagnostics, profiles, manager, failed, success, observer = {}, {}, {}, {}, {}, {}, {}
    broker_dev, broker_ino = (int(part) for part in readback["broker"]["tuple"].split(":")[:2])
    for index, (unit, outcome) in enumerate((("buster-bench-systemd-broker@0-734-65000.service", 0),
                                             ("buster-bench-systemd-broker@5-833-0.service", 1))):
        invocation = f"{index + 1:x}" * 32
        pid, ticks, stream = 700 + index, 43000 + index, f"{index + 7:x}" * 32
        key = (boot, unit, invocation, pid)
        passes[key] = {"boot_id_raw": boot_raw, "pid": pid, "ticks": ticks, "invocation": invocation,
                       "cgroup": prefix + unit, "socket": (8, 500 + index),
                       "tuples": {name: readback[name]["tuple"] for name in
                                  ("gate", "broker", "receipt", "passwd", "group", "nsswitch")},
                       "accounts": list(entry["accounts"]),
                       "hashes": {name: readback[name]["sha256"] for name in ("passwd", "group", "nsswitch")},
                       "gid": 65000, "groups": (65001, 65000), "record_number": 10 * index + 1,
                       "stream_id": stream, "unit": unit}
        diagnostics[key] = {"stream_id": stream, "record_numbers": [10 * index + 2, 10 * index + 3],
                            "identifiers": {STREAM_IDENTIFIER},
                            "sequence": {"start_ticks": ticks, "stat": {},
                                         "socket": {"dev": 8, "ino": 500 + index, "mode": 0o140777},
                                         "exe": {"path": BROKER_PATH, "dev": broker_dev, "ino": broker_ino},
                                         "outcome": {"broker_exit": outcome},
                                         "request": {"request_known": outcome == 0,
                                                     "peer_uid": 65000 if outcome == 0 else 0}}}
        profiles[key] = {"cgroup_path": prefix + unit}
        manager[(boot, unit)] = {"result": "done", "invocation_id": invocation}
        if outcome == 0:
            success[(boot, unit, invocation)] = 10 * index + 5
            observer[unit] = {"new": 1, "removed": 1, "main_pids": {pid}, "retained": None}
        else:
            failed[(boot, unit, invocation)] = {"exit": {"code": "exited", "status": "1", "monotonic_us": 2000},
                                                "failure": {"result": "exit-code", "monotonic_us": 2001}}
            observer[unit] = {"new": 1, "removed": 0, "main_pids": {pid},
                              "retained": {"InvocationID": invocation, "ExecMainPID": str(pid),
                                           "ExecMainExitTimestampMonotonic": "1999",
                                           "query_started_monotonic_ns": "3000000"}}
    return {"passes": passes, "diagnostics": diagnostics, "profiles": profiles, "manager_rows": manager,
            "failed_rows": failed, "success_rows": success, "observer_units": observer,
            "readback": readback, "expected": expected, "entry": entry}


def _pass_line(fixture: dict[str, Any], key: tuple[str, str, str, int]) -> str:
    record = fixture["passes"][key]
    tuples, hashes, accounts = record["tuples"], record["hashes"], record["accounts"]
    return (f"BQ-BROKER-ENTRY-V1 PASS boot={record['boot_id_raw']} pid={record['pid']} ticks={record['ticks']} "
            f"invocation={record['invocation']} cgroup={record['cgroup']} "
            f"socket={record['socket'][0]}:{record['socket'][1]} gate={tuples['gate']} "
            f"broker={tuples['broker']} receipt={tuples['receipt']} accounts={','.join(map(str, accounts))} "
            f"passwd={tuples['passwd']} group={tuples['group']} nsswitch={tuples['nsswitch']} "
            f"passwd-sha256={hashes['passwd']} group-sha256={hashes['group']} "
            f"nsswitch-sha256={hashes['nsswitch']} uid=0 gid=65000 groups=65000,65001 "
            "caps=0 nnp=1 seccomp=2 mounts=ro fd0=seqpacket")


def self_test() -> int:
    import copy
    checks = 0

    def join(fixture: dict[str, Any]) -> list[dict[str, Any]]:
        return join_activations(**fixture)

    def rejects(label: str, mutate) -> None:
        nonlocal checks
        fixture = copy.deepcopy(_fixture())
        mutate(fixture)
        try:
            join(fixture)
        except EvidenceError:
            checks += 1
            return
        raise AssertionError(f"accepted mutation: {label}")

    rows = join(_fixture())
    assert [row["outcome"] for row in rows] == ["success", "exit-code"]
    checks += 1
    fixture = _fixture()
    for key in fixture["passes"]:
        line = _pass_line(fixture, key)
        parsed = parse_pass(line)
        for field in ("pid", "ticks", "invocation", "cgroup", "socket", "tuples", "accounts", "hashes"):
            assert parsed[field] == fixture["passes"][key][field] or (
                field == "socket" and parsed[field] == tuple(fixture["passes"][key][field])), field
        checks += 1
        for bad in (line.replace("groups=65000,65001", "groups=0,65000,65001"),
                    line.replace(" nnp=1 ", " nnp=0 "), line.replace(" caps=0 ", " caps=1 "),
                    line + " ", line.replace("PASS", "FAIL"), line.replace("seccomp=2", "seccomp=0"),
                    line.replace(" mounts=ro", " mounts=rw"), line.replace("pid=", "pid=0")):
            try:
                parse_pass(bad)
            except EvidenceError:
                checks += 1
                continue
            raise AssertionError(f"accepted malformed PASS: {bad[:80]}")
    first, second = sorted(_fixture()["passes"], key=lambda key: key[1])
    rejects("missing PASS", lambda f: f["passes"].pop(first))
    rejects("PASS without broker", lambda f: f["diagnostics"].pop(first))
    rejects("missing PID1 start", lambda f: f["manager_rows"].pop((first[0], first[1])))
    rejects("PID1 starts other invocation",
            lambda f: f["manager_rows"][(first[0], first[1])].update(invocation_id="f" * 32))
    rejects("failed start job", lambda f: f["manager_rows"][(first[0], first[1])].update(result="failed"))
    rejects("missing observer lifecycle", lambda f: f["observer_units"].pop(first[1]))
    rejects("observer other MainPID", lambda f: f["observer_units"][first[1]].update(main_pids={1}))
    rejects("observer extra MainPID", lambda f: f["observer_units"][first[1]]["main_pids"].add(9))
    rejects("observer no MainPID", lambda f: f["observer_units"][first[1]].update(main_pids=set()))
    rejects("successful unit not removed", lambda f: f["observer_units"][first[1]].update(removed=0))
    rejects("ticks differ", lambda f: f["passes"][first].update(ticks=1))
    rejects("stream differs", lambda f: f["passes"][first].update(stream_id="0" * 32))
    rejects("PASS after BEGIN", lambda f: f["passes"][first].update(record_number=99))
    rejects("FD0 differs", lambda f: f["passes"][first].update(socket=(8, 1)))
    rejects("cgroup differs", lambda f: f["passes"][first].update(cgroup="/other.service"))
    rejects("accounts differ", lambda f: f["passes"][first]["accounts"].__setitem__(4, 65003))
    rejects("group 0 admitted", lambda f: f["passes"][first].update(groups=(0, 65000)))
    rejects("boot differs", lambda f: f["passes"][first].update(boot_id_raw="0" * 8 + "-0000-0000-0000-" + "0" * 12))
    for name in ("gate", "broker", "receipt", "passwd", "group", "nsswitch"):
        rejects(f"{name} tuple differs",
                lambda f, name=name: f["passes"][first]["tuples"].__setitem__(name, _fixture_tuple(77)))
    for name in ("passwd", "group", "nsswitch"):
        rejects(f"{name} hash differs",
                lambda f, name=name: f["passes"][first]["hashes"].__setitem__(name, "e" * 64))
    rejects("broker exe differs", lambda f: f["diagnostics"][first]["sequence"]["exe"].update(ino=1))
    rejects("success with failure exit",
            lambda f: f["diagnostics"][first]["sequence"]["outcome"].update(broker_exit=1))
    rejects("missing PID1 terminal", lambda f: f["success_rows"].clear())
    rejects("double PID1 terminal", lambda f: f["failed_rows"].__setitem__(
        (first[0], first[1], first[2]), {"exit": {"code": "exited", "status": "1", "monotonic_us": 1},
                                         "failure": {"result": "exit-code", "monotonic_us": 2}}))
    rejects("broker off the gate stream", lambda f: f["diagnostics"][first].update(
        identifiers={"buster-bench-systemd-broker"}))
    rejects("mixed stream identifiers", lambda f: f["diagnostics"][first]["identifiers"].add("sh"))
    rejects("retained queried before exit", lambda f: f["observer_units"][second[1]]["retained"].update(
        query_started_monotonic_ns="1000"))
    rejects("PID1 failure precedes exit", lambda f: f["observer_units"][second[1]]["retained"].update(
        ExecMainExitTimestampMonotonic="2500"))
    rejects("failure with success exit",
            lambda f: f["diagnostics"][second]["sequence"]["outcome"].update(broker_exit=0))
    rejects("retained other invocation",
            lambda f: f["observer_units"][second[1]]["retained"].update(InvocationID="f" * 32))
    rejects("retained other PID", lambda f: f["observer_units"][second[1]]["retained"].update(ExecMainPID="1"))
    rejects("failed unit removed", lambda f: f["observer_units"][second[1]].update(removed=1))
    rejects("no activations", lambda f: [f[key].clear() for key in ("passes", "diagnostics")])
    good = _fixture()
    summary = {"units": [{"unit": unit, "snapshots": [{"complete": True, "process_capture_complete": True,
                                                       "main_pid": key[3], "invocation_id": key[2],
                                                       "start_ticks": good["passes"][key]["ticks"]}]}
                         for key, unit in ((key, key[1]) for key in good["passes"])]}
    joined = join(good)
    assert external_consistency(summary, joined, [])["complete_external_captures_checked"] == 2
    checks += 1
    summary["units"][0]["snapshots"][0]["start_ticks"] = 1
    try:
        external_consistency(summary, joined, [])
    except EvidenceError:
        checks += 1
    else:
        raise AssertionError("accepted contradictory external capture")
    try:
        external_consistency({"units": []}, joined, [{"unit": "buster-bench-systemd-broker@9-1-65000.service"}])
    except EvidenceError:
        checks += 1
    else:
        raise AssertionError("accepted out-of-population live capture")
    readback_doc = {"schema": READBACK_SCHEMA, "files": good["readback"]}
    assert parse_readback(json.dumps(readback_doc).encode(), "fixture") == good["readback"]
    checks += 1
    for name, field, value in (("gate", "mode", 0o100775), ("receipt", "uid", 65000), ("broker", "nlink", 2),
                               ("passwd", "mode", 0o100666), ("nsswitch", "bytes", 1)):
        changed = copy.deepcopy(readback_doc)
        changed["files"][name][field] = value
        try:
            parse_readback(json.dumps(changed).encode(), "fixture")
        except EvidenceError:
            checks += 1
        else:
            raise AssertionError(f"accepted readback {name}.{field}")
    checks += _journal_and_observer_self_test(good)
    print(f"BROKER_ENTRY_EVIDENCE_SELF_TEST checks={checks} failures=0 fixtures-only-not-live-proof")
    return 0


def _journal_and_observer_self_test(good: dict[str, Any]) -> int:
    import copy
    import tempfile
    checks = 0
    boot = good["expected"]["boot_id"]

    def expect_error(label: str, call) -> None:
        nonlocal checks
        try:
            call()
        except EvidenceError:
            checks += 1
            return
        raise AssertionError(f"accepted: {label}")

    key = sorted(good["passes"])[0]
    record = {"MESSAGE": _pass_line(good, key), "_TRANSPORT": "stdout", "_BOOT_ID": boot,
              "_SYSTEMD_UNIT": key[1], "_SYSTEMD_INVOCATION_ID": key[2], "_PID": str(key[3]),
              "_STREAM_ID": "7" * 32, "SYSLOG_IDENTIFIER": STREAM_IDENTIFIER}
    passes = collect_passes([record], boot)
    assert list(passes) == [key] and passes[key]["record_number"] == 1
    checks += 1
    for label, change in (("wrong trusted PID", {"_PID": "1"}),
                          ("wrong invocation", {"_SYSTEMD_INVOCATION_ID": "f" * 32}),
                          ("journal transport", {"_TRANSPORT": "journal"}),
                          ("wrong boot", {"_BOOT_ID": "0" * 32}),
                          ("non-broker unit", {"_SYSTEMD_UNIT": "sshd.service"}),
                          ("other stream identifier", {"SYSLOG_IDENTIFIER": "buster-bench-systemd-broker"}),
                          ("split line", {"_LINE_BREAK": "line-max"}),
                          ("marker inside other text", {"MESSAGE": "note " + record["MESSAGE"]}),
                          ("marker in byte array", {"MESSAGE": list(b"x BQ-BROKER-ENTRY-V1 PASS")})):
        changed = dict(record)
        changed.update(change)
        expect_error(label, lambda changed=changed: collect_passes([changed], boot))
    expect_error("duplicate PASS", lambda: collect_passes([record, dict(record)], boot))
    success = {"MESSAGE_ID": UNIT_SUCCESS_MESSAGE_ID, "UNIT": key[1], "_PID": "1", "_TRANSPORT": "journal",
               "_BOOT_ID": boot, "INVOCATION_ID": key[2]}
    assert collect_success_terminals([success], boot) == {(boot, key[1], key[2]): 1}
    checks += 1
    expect_error("success not from PID1", lambda: collect_success_terminals([dict(success, _PID="2")], boot))
    expect_error("duplicate success", lambda: collect_success_terminals([success, dict(success)], boot))

    unit = key[1]
    path = base._unit_object_path(unit)
    def event(seq: int, member: str, **extra: Any) -> dict[str, Any]:
        row = {"seq": seq, "unit": unit, "member": member, "generation": 1, "sender": ":1.0",
               "object_path": path, "monotonic_ns": 100 + seq,
               "interface": PROPERTIES_INTERFACE if member == "PropertiesChanged" else MANAGER_INTERFACE}
        if member == "PropertiesChanged":
            row.update(unit_interface="org.freedesktop.systemd1.Service", main_pids=[0, key[3]])
        row.update(extra)
        return row
    events = [event(1, "UnitNew"), event(2, "PropertiesChanged"),
              event(3, "PropertiesChanged", unit_interface="org.freedesktop.systemd1.Unit", main_pids=[]),
              event(4, "UnitRemoved")]
    summary = {"event_stream_complete": True, "event_loss_detected": False, "stop_seen": True,
               "global_incomplete_reasons": [], "manager_sender": ":1.0", "boot_id": boot,
               "units": [{"unit": unit, "generation": 1, "boot_id": boot}]}
    units = lifecycles_from_events(events, summary)
    assert units[unit]["main_pids"] == {key[3]} and units[unit]["removed"] == 1
    checks += 1
    for label, mutate in (
            ("event loss", lambda e, s: s.update(event_loss_detected=True)),
            ("no stop", lambda e, s: s.update(stop_seen=False)),
            ("foreign sender", lambda e, s: e[1].update(sender=":1.99")),
            ("wrong interface", lambda e, s: e[0].update(interface=PROPERTIES_INTERFACE)),
            ("wrong object path", lambda e, s: e[1].update(object_path="/org/freedesktop/systemd1/unit/x")),
            ("second generation", lambda e, s: e[3].update(generation=2)),
            ("change after removal", lambda e, s: e.append(event(5, "PropertiesChanged"))),
            ("removed twice", lambda e, s: e.append(event(5, "UnitRemoved"))),
            ("properties before UnitNew", lambda e, s: e.insert(0, event(0, "PropertiesChanged"))),
            ("time reversal", lambda e, s: e[3].update(monotonic_ns=1)),
            ("duplicate summary row", lambda e, s: s["units"].append(dict(s["units"][0]))),
            ("summary without signals", lambda e, s: s["units"].append(
                {"unit": "buster-bench-systemd-broker@9-1-65000.service", "generation": 1, "boot_id": boot})),
            ("other signal member", lambda e, s: e.append(event(5, "JobNew"))),
            ("MainPID on the unit interface", lambda e, s: e[2].update(main_pids=[key[3]])),
            ("foreign property interface", lambda e, s: e[1].update(unit_interface="org.freedesktop.systemd1.Socket"))):
        changed_events, changed_summary = copy.deepcopy(events), copy.deepcopy(summary)
        mutate(changed_events, changed_summary)
        expect_error(label, lambda e=changed_events, s=changed_summary: lifecycles_from_events(e, s))

    with tempfile.TemporaryDirectory() as directory:
        document = {"schema": READBACK_SCHEMA, "files": good["readback"]}
        before, after = Path(directory, "before.json"), Path(directory, "after.json")
        before.write_text(json.dumps(document))
        after.write_text(json.dumps(document))
        entry = {"gate_sha256": good["readback"]["gate"]["sha256"],
                 "broker_sha256": good["readback"]["broker"]["sha256"],
                 "readbacks": [str(before), str(after)]}
        assert load_readbacks(entry) == good["readback"]
        checks += 1
        changed = copy.deepcopy(document)
        changed["files"]["group"]["sha256"] = "d" * 64
        after.write_text(json.dumps(changed))
        expect_error("readback changed during run", lambda: load_readbacks(entry))
        after.write_text(json.dumps(document))
        expect_error("gate bytes differ from payload", lambda: load_readbacks(dict(entry, gate_sha256="c" * 64)))
    return checks


if __name__ == "__main__":
    sys.exit(main())
