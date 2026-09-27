#!/usr/bin/env python3
"""Offline fail-closed consumer for private #1162 broker evidence.

The consumer never invokes systemd or a broker. It preserves separate
component results; a complete diagnostic stream is not a manager or security
acceptance result.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tempfile
import zlib
from typing import Any

SCHEMA = "issue1162-broker-evidence-expectation-v1"
OBS_SCHEMA = "issue1162-broker-observer-v1"
SUPERSEDABLE_CAPTURE_STATES = frozenset(("transient_proc_disappeared",
                                        "transient_proc_interrupted",
                                        "transient_capture_deadline",
                                        "known_preexec_executable_transition"))
MAX_FILE = 128 * 1024 * 1024
MAX_JSON_LINE = 4 * 1024 * 1024
MAX_OBSERVER_EVENTS = 16 * 1024 * 1024
MAX_OBSERVER_SUMMARY = 4 * 1024 * 1024
MAX_OVERLAP = 16 * 1024 * 1024
MAX_INSTANCES = 128
MAX_DIAG_BYTES = 512 * 1024
MAX_DIAG_LINE = 1200
TEMPLATE_METADATA_UNIT = "buster-bench-systemd-broker@internal.service"
TEMPLATE_METADATA_PATH = ("/org/freedesktop/systemd1/unit/"
                          "buster_2dbench_2dsystemd_2dbroker_40internal_2eservice")
TEMPLATE_METADATA_FIELDS = frozenset((
    "boot_id", "boot_id_raw", "unit", "object_path", "sender", "member",
    "interface", "path", "signature", "bus_dispatch_ordinal", "monotonic_ns"))
JOURNAL_UNIT_FIELDS = ("UNIT", "_SYSTEMD_UNIT", "OBJECT_SYSTEMD_UNIT", "COREDUMP_UNIT")
DATA_FIELDS = ("stat", "status", "mountinfo", "cgroup", "exe", "socket")
DATA_LIMITS = {"stat": 4096, "status": 16384, "mountinfo": 131072,
               "cgroup": 4096, "exe": 767, "socket": 255}
PREEXEC_EXECUTABLES = frozenset(("/lib/systemd/systemd-executor",
                                 "/usr/lib/systemd/systemd-executor"))
UNIT_RE = re.compile(
    r"buster-bench-systemd-broker@(?P<counter>0|[1-9][0-9]*)-"
    r"(?P<peer_pid>[1-9][0-9]*)-(?P<peer_uid>0|[1-9][0-9]*)\.service\Z")
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
UUID36 = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\Z")
STARTING_MESSAGE_ID = "7d4958e842da4a758f6c1cdc7b36dcc5"
STARTED_MESSAGE_ID = "39f53479d3a045ac8e11786248231fbf"
FAILED_MESSAGE_ID = "be02cf6855d2428ba40df7e9d022f03d"
PROCESS_EXIT_MESSAGE_ID = "98e322203f7a4ed290d09fe03c09fe15"
UNIT_FAILED_MESSAGE_ID = "d9b373ed55a64feb8242e02dbe79a49c"
TERMINAL_PROPERTIES = frozenset((
    "Id", "LoadState", "CollectMode", "ActiveState", "SubState", "MainPID",
    "InvocationID", "ExecMainPID", "ExecMainCode", "ExecMainStatus",
    "ExecMainStartTimestampMonotonic", "ExecMainExitTimestampMonotonic", "Result"))
TERMINAL_FIELDS = frozenset((
    "boot_id", "boot_id_raw", "unit", "object_path", "generation", "manager_sender",
    "event_seq", "started_monotonic_ns", "finished_monotonic_ns", "raw_show", "raw_stderr",
    "systemctl_exit", "systemctl_timed_out", "systemctl_timeout_reason", "systemctl_cancelled",
    "manager_properties_complete", "incomplete_reasons", "properties"))

# Captured from the frozen #1472 C emitter through an AF_UNIX socketpair.
# The producer's REQUEST/OUTCOME values are simulated, and the stream has no
# journal trust metadata; it is a grammar fixture only, never run evidence.
_PRODUCER_FIXTURE_ZLIB_B64 = (
    "eNrVWtty2zYQfe9X6AcygzvIBz3YjSfNdBo3idNXD8WLo1qWXEtO8/k9u4AogqRsy1bc1hyOcFksDvaGBejTj29OP53/evbpzdv3J+/e/CEnp2fv3n+Y3M6rqRSTzby8Xk9VJqQ1+qfTAfHbk4uTUdpJM68X1XS9KTaTVdOs680UFKtNsZhqpSZf6+9TbbTVudZaaKeEyrzwyjXOeOu0s17ZBr8SrWjxSuVKWKVEHKXwZknND2oCXCstuV2izfBcBnWFug4Ue1/VlmTbkmG8xByGRyvt0EKtWaCjMv9moDDo9fwr8KvxCmCweCR6wEUJxuQYneBSkIfF60Z6Hfh71CSvTkbOirk6jFE8G5XH1mN4fjfSE/jtk8cQheE17MNoWCr9sTS74HXnXMr2riDnR+/ptzzavKDfoMUG+xDFM036ft03aglDZas2tZOuclYXIn/YokVhLSil125G1IxcaWrX3qDdBC5k9fAOBS41nhyvVzmojPOoGR7b8SVRAMOuB6u0YpzOikEP+xXxBnIZUYuEl+3Uum9h/N4eZwwUkPsirIhsguhJOpCEgHugVRFlzWsfX1dOVhZpxBMo/MjaPNFECv1A/wwoIHNnIYXd2p2rIA1qmwXE4eFIYMji3cyoSNdZb0KnUjpTOp1yi7QpN1pv/RiVyaw3VUqFZcIDEl6KgtCDVIqqRsLSmv6sZKHkvQmlA7oyXSsoyddMSgmpZOQdrnp0LcEDZJ+O9JTKGVQ9+XGMTqiM9VkfH8U8imGJLnKnEo1JjuFZqgtL5tznFfX/4ohipRrElN1aPcWPh2RnMsQJD7810Aj5ruSafnCMhh/C1g1Fqop9CxGktXoDuxKIXRR/pFOk6egT8BSKSqQn50GRRDNI/JE/jmGtlwWMxJN8B/xk4KMa6AmeSZpv+wSwmDZqPvDwiMyZA0dgDkMrnR00Am/wl720sjdCO++fiMrw3mFIN9khIyztQtVBc1jSzkEj1NOlG0fQLqmeOAJxCK/1nteSe5fYCu2usGCyXG5V/VZYJkco2lGxj3bGeormGjZccrTJKd7ZhmMKe4RtjPLYx8muwzjMTdSERm29YR8n1hXKbOHgCV40pgYu1iDKJXt0juArUv8iOWFnhOfa5sUxRQpl+kGFJsfTAEDrzsEpdxOnNKgjQEIQUdFIrpEqGA7nQ+ox1aqy7w77x/dnQ6hxEGq5DThQZE4JFZKpzIPaU3DM0UPuHgMejQqb2PPGPkPwN6v75Wa+bFb9DFEJ48O5J2S3LmTcuqCsHvG4CS+geUX2RaA41wo1R7brVMm2tLUxiI7OOLCdBgski6SefFiPXKNw0c5WqSvVhLxSNW4W9ltVsdeEmJ4HO4d1NjwKfoRyBXVZ9qYcZZ6BehGnQ3afYfelM5GkJTrOAij55lbkWtjT4QV8GlGct2nOzcFDNTuEMC6YC2eH4r+MeIcwKZNfUwSZ/S9wR6yqdCFiIVvSFWFwsDiyMsZIWYLweacHGD2N0LoKDhbtkmMiIeB8bWvnmk/kbOfxZKQft3cuc/YG6j3W3+GW8GUM8ZzdYpA0P6ROfMoEC2nGhlj8CKowq+FcyTF9t8xj/Tb/r/hcz2d6lnB1jKCySxLbsBLAYTrKsoP43dOXvhUoH1Jj3P2xYhjB63t42Vycor2op6jYRvXDg+WD5pKNYnADBO4HzZ+Pzo8Q4mUPQWw7Oga5u7xJUZCz9+WwbfshKGQvfMht+Nja36vaJyFSSrTIAibVw9Tw9c6RAlug3XJMa5F/uOSotD1GWOnkiW1cSWQRri9KHySZJT2OrpRYOronJcUXvKmU+CLguelOoscOpzi/Gcxv+/NjvMR8lt4j6SrhOOQfsdnEplVnS3yiJz1pQ2ReW/l0yjscLsrItkhMD0mIcXRl2V7UHQddlyNf0/TqW20AteHxdKHB16ERux94oTswMjzFrgaxYk9MyCIav3+fba2ubRvBd/xolY8jIw6sx64/tm19ZEeJKziPD/KV469Yxs8ZQ124EV24V9SFZFsd00W4EDB0UZHoI2l/FYRqD0K7F6P9F1DqPSgNnjxBF1teBZUZoPIH5wUvzwUO26fDJ8gu6lfcp58RVcqru9X9bf9axYY7FYJfqOZZjOvv9eBznpLMl2/+jMv4/B5PyZ7u+vjbGItHkxOEqyOITWpPR2Ocu2ECTrHZKKf4yEIJ7izcbdCtAN/MB4FSjU77DkaXha+DzP3B74ecZrD4oVa+Q+LTe4Oa5G/Q9FXM0CbQUX+4f2bTZZV0jZy+Urvn3DWuyut68JlfZTHMR4RZB5/l7015cJMEXfhii2cEx+cPJ79//uX84vLsw9txPHTryf9fEK4/w2VnuxuFvSeYEewGamc9B/iEt14Ut+u6urxZT8XI/J/OPn45+3wxPvVtXd9dXi9Xfy+nMlQiGZfvUUZxtVhclsViUaNncleX39LaVHo3aRbF1ZqIi7s199Ba6svyaw2Yu/q3YjHn2vxqWSzabrFtCP1iUq5ubopl1Rm/bdlyuKv/uq/Xmxb86ra+Kzbz1TLMdVVPzeTP1QzCnBSbTX1zi/18RDrnXy5+Pv/tbFw6s7vVNcRQf5+TgTR3xU19GXXUVuslMf4HbfVELg=="
)


class EvidenceError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def canonical_uint(text: str, label: str, maximum: int = (1 << 64) - 1,
                   allow_zero: bool = True) -> int:
    require(isinstance(text, str) and bool(re.fullmatch(r"0|[1-9][0-9]*", text)),
            f"{label} is not canonical unsigned decimal")
    require(len(text) <= 20, f"{label} exceeds decimal bound")
    value = int(text, 10)
    require(value <= maximum and (allow_zero or value > 0), f"{label} is out of range")
    return value


def canonical_signed(text: str, label: str, minimum: int, maximum: int) -> int:
    require(isinstance(text, str) and bool(re.fullmatch(r"0|-?[1-9][0-9]*", text)),
            f"{label} is not canonical signed decimal")
    require(len(text) <= 21, f"{label} exceeds decimal bound")
    value = int(text, 10)
    require(minimum <= value <= maximum, f"{label} is out of range")
    return value


def boot_uuid(raw: str, label: str) -> str:
    require(isinstance(raw, str) and UUID36.fullmatch(raw) is not None,
            f"{label} is not a lowercase UUID36")
    return raw.replace("-", "")


def parse_broker_unit(value: Any, label: str) -> re.Match[str]:
    require(isinstance(value, str) and len(value) <= 128,
            f"{label} is not a bounded broker unit string")
    match = UNIT_RE.fullmatch(value)
    require(match is not None, f"{label} is not a canonical broker instance")
    canonical_uint(match.group("counter"), f"{label} instance counter", (1 << 64) - 1)
    canonical_uint(match.group("peer_pid"), f"{label} peer PID", (1 << 31) - 1, False)
    canonical_uint(match.group("peer_uid"), f"{label} peer UID", (1 << 32) - 1)
    return match


def _no_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_json(raw: bytes, label: str) -> Any:
    require(len(raw) <= MAX_FILE, f"{label} exceeds file bound")
    try:
        value = json.loads(raw.decode("utf-8", "strict"), object_pairs_hook=_no_duplicate_pairs,
                           parse_constant=lambda token: (_ for _ in ()).throw(
                               EvidenceError(f"invalid JSON constant in {label}: {token}")))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise EvidenceError(f"invalid JSON in {label}: {exc}") from exc
    stack: list[tuple[Any, int]] = [(value, 0)]
    nodes = 0
    while stack:
        current, depth = stack.pop()
        nodes += 1
        require(nodes <= 1_000_000 and depth <= 32, f"JSON structure exceeds bounds in {label}")
        if isinstance(current, dict):
            stack.extend((item, depth + 1) for item in current.values())
        elif isinstance(current, list):
            stack.extend((item, depth + 1) for item in current)
    return value


def _stable(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_uid,
            info.st_gid, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def open_absolute_directory(path: str) -> int:
    require(isinstance(path, str) and path.startswith("/"), "input directory must be absolute")
    current = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in PurePosixPath(path).parts[1:]:
            require(part not in ("", ".", ".."), "invalid input directory component")
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=current)
            os.close(current)
            current = child
        require(stat.S_ISDIR(os.fstat(current).st_mode), "input path is not a directory")
        return current
    except Exception:
        os.close(current)
        raise


def read_regular_at(directory_fd: int, relative: str, maximum: int,
                    allow_empty: bool = False) -> bytes:
    pure = PurePosixPath(relative)
    require(not pure.is_absolute() and bool(pure.parts) and
            all(part not in ("", ".", "..") for part in pure.parts),
            "unsafe artifact reference path")
    current = os.dup(directory_fd)
    try:
        for part in pure.parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                            dir_fd=current)
            os.close(current)
            current = child
        fd = os.open(pure.parts[-1], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=current)
        try:
            before = os.fstat(fd)
            require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and
                    (0 <= before.st_size if allow_empty else 0 < before.st_size) and
                    before.st_size <= maximum, f"unsafe or oversized artifact: {relative}")
            chunks = bytearray()
            while len(chunks) < before.st_size:
                block = os.read(fd, min(1024 * 1024, before.st_size - len(chunks)))
                require(bool(block), f"short artifact read: {relative}")
                chunks.extend(block)
            after = os.fstat(fd)
            path_after = os.stat(pure.parts[-1], dir_fd=current, follow_symlinks=False)
            require(_stable(before) == _stable(after) == _stable(path_after) and
                    os.read(fd, 1) == b"", f"artifact changed while read: {relative}")
            return bytes(chunks)
        finally:
            os.close(fd)
    finally:
        os.close(current)


def read_absolute_file(path: str, maximum: int, allow_empty: bool = False) -> bytes:
    require(isinstance(path, str) and path.startswith("/"), "input file must be absolute")
    pure = PurePosixPath(path)
    parent = str(pure.parent)
    name = pure.name
    require(name not in ("", ".", ".."), "invalid input filename")
    directory_fd = open_absolute_directory(parent)
    try:
        return read_regular_at(directory_fd, name, maximum, allow_empty)
    finally:
        os.close(directory_fd)


def parse_jsonl(raw: bytes, label: str, line_limit: int = MAX_JSON_LINE) -> list[dict[str, Any]]:
    require(raw.endswith(b"\n"), f"{label} is not newline-terminated")
    records: list[dict[str, Any]] = []
    start = 0
    for number, end in enumerate((index for index, byte in enumerate(raw) if byte == 10), 1):
        line = raw[start:end]
        start = end + 1
        require(bool(line) and len(line) <= line_limit, f"empty or oversized line {number} in {label}")
        value = strict_json(line, f"{label} line {number}")
        require(isinstance(value, dict), f"non-object record at {label} line {number}")
        records.append(value)
        require(len(records) <= 1_000_000, f"too many records in {label}")
    return records


def _parse_message(message: str) -> tuple[str, dict[str, Any]]:
    require(isinstance(message, str) and "\n" not in message and "\r" not in message and
            "\x00" not in message and len(message.encode("utf-8", "strict")) < MAX_DIAG_LINE,
            "diagnostic message is not one bounded text line")
    prefix = "BQ-BROKER-DIAG-V1 "
    require(message.startswith(prefix), "malformed broker diagnostic prefix")
    body = message[len(prefix):]
    match = re.fullmatch(r"BEGIN pid=(0|[1-9][0-9]*) ticks=(0|[1-9][0-9]*)", body)
    if match:
        return "BEGIN", {"pid": canonical_uint(match.group(1), "pid", (1 << 31) - 1, False),
                          "ticks": canonical_uint(match.group(2), "ticks", (1 << 64) - 1, False)}
    match = re.fullmatch(
        r"DATA pid=(0|[1-9][0-9]*) ticks=(0|[1-9][0-9]*) field=(stat|status|mountinfo|cgroup|exe|socket) "
        r"offset=(0|[1-9][0-9]*) total=(0|[1-9][0-9]*) hex=([0-9a-f]+)", body)
    if match:
        pid = canonical_uint(match.group(1), "pid", (1 << 31) - 1, False)
        ticks = canonical_uint(match.group(2), "ticks", (1 << 64) - 1, False)
        offset = canonical_uint(match.group(4), "data offset", 131072)
        total = canonical_uint(match.group(5), "data total", DATA_LIMITS[match.group(3)], False)
        encoded = match.group(6)
        require(len(encoded) <= 1024 and len(encoded) % 2 == 0,
                "diagnostic data chunk exceeds producer chunk bound")
        return "DATA", {"pid": pid, "ticks": ticks, "field": match.group(3),
                         "offset": offset, "total": total, "hex": encoded}
    match = re.fullmatch(
        r"SNAPSHOT_END pid=(0|[1-9][0-9]*) ticks=(0|[1-9][0-9]*) "
        r"stat=(0|[1-9][0-9]*) status=(0|[1-9][0-9]*) mountinfo=(0|[1-9][0-9]*) "
        r"cgroup=(0|[1-9][0-9]*) exe=(0|[1-9][0-9]*) socket=(0|[1-9][0-9]*) "
        r"elapsed_ms=(0|[1-9][0-9]*)", body)
    if match:
        fields = ("stat", "status", "mountinfo", "cgroup", "exe", "socket")
        result = {"pid": canonical_uint(match.group(1), "pid", (1 << 31) - 1, False),
                  "ticks": canonical_uint(match.group(2), "ticks", (1 << 64) - 1, False),
                  "elapsed_ms": canonical_uint(match.group(9), "elapsed_ms", 500)}
        for field, index in zip(fields, range(3, 9)):
            result[field] = canonical_uint(match.group(index), f"{field} size", DATA_LIMITS[field], False)
        return "SNAPSHOT_END", result
    match = re.fullmatch(
        r"REQUEST pid=(0|[1-9][0-9]*) ticks=(0|[1-9][0-9]*) "
        r"peer_known=([01]) peer_pid=(0|[1-9][0-9]*) peer_uid=(0|[1-9][0-9]*) "
        r"poll_called=([01]) recv_called=([01]) recv=(-?[0-9]+) flags=(0|[1-9][0-9]*) "
        r"parsed=([01]) state_checked=([01]) state_valid=([01]) signal_checked=([01]) signal_valid=([01]) "
        r"command_checked=([01]) command_valid=([01]) request_known=([01]) "
        r"operation=(0|[1-9][0-9]*) stage=(0|[1-9][0-9]*) job=(0|[1-9][0-9]*) attempt=(0|[1-9][0-9]*)", body)
    if match:
        names = ("peer_known", "peer_pid", "peer_uid", "poll_called", "recv_called", "parsed",
                 "state_checked", "state_valid", "signal_checked", "signal_valid", "command_checked",
                 "command_valid", "request_known", "operation", "stage", "job", "attempt")
        positions = (3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21)
        result: dict[str, Any] = {
            "pid": canonical_uint(match.group(1), "pid", (1 << 31) - 1, False),
            "ticks": canonical_uint(match.group(2), "ticks", (1 << 64) - 1, False),
            "recv": canonical_signed(match.group(8), "recv", -(1 << 31), (1 << 31) - 1),
            "flags": canonical_uint(match.group(9), "flags", (1 << 32) - 1),
        }
        # The flags group precedes the boolean groups; keep the explicit index map auditable.
        for name, index in zip(names, positions):
            if name == "peer_pid":
                result[name] = canonical_uint(match.group(index), name, (1 << 31) - 1)
            elif name == "peer_uid":
                result[name] = canonical_uint(match.group(index), name, (1 << 32) - 1)
            elif name in ("operation", "stage", "job", "attempt"):
                result[name] = canonical_uint(match.group(index), name, (1 << 64) - 1)
            else:
                result[name] = canonical_uint(match.group(index), name, 1)
        return "REQUEST", result
    match = re.fullmatch(
        r"OUTCOME pid=(0|[1-9][0-9]*) ticks=(0|[1-9][0-9]*) "
        r"broker_exit=(-?[0-9]+) frame_status=(-?[0-9]+) frame_sent=([01])", body)
    if match:
        return "OUTCOME", {
            "pid": canonical_uint(match.group(1), "pid", (1 << 31) - 1, False),
            "ticks": canonical_uint(match.group(2), "ticks", (1 << 64) - 1, False),
            "broker_exit": canonical_signed(match.group(3), "broker_exit", -(1 << 31), (1 << 31) - 1),
            "frame_status": canonical_signed(match.group(4), "frame_status", -(1 << 31), (1 << 31) - 1),
            "frame_sent": canonical_uint(match.group(5), "frame_sent", 1)}
    raise EvidenceError("unknown or malformed BQ-BROKER-DIAG-V1 record")


def _check_request_semantics(request: dict[str, Any]) -> None:
    require(request["peer_known"] or request["peer_pid"] == request["peer_uid"] == 0,
            "unknown peer exposes peer credentials")
    require(not request["peer_known"] or request["peer_pid"] > 0,
            "known peer has zero PID")
    require(not (request["poll_called"] and not request["peer_known"]),
            "poll called without known peer")
    require(not (request["recv_called"] and not request["poll_called"]),
            "recv called without poll")
    require(not (request["parsed"] and not request["recv_called"]),
            "parsed request without recv")
    require(not (request["state_checked"] and not request["parsed"]),
            "state checked without parsed request")
    require(not (request["state_valid"] and not request["state_checked"]),
            "state valid without state check")
    require(not (request["signal_checked"] and not request["state_valid"]),
            "signal checked without valid state")
    require(not (request["signal_valid"] and not request["signal_checked"]),
            "signal valid without signal check")
    require(not (request["command_checked"] and not request["state_valid"]),
            "command checked without valid state")
    require(not (request["command_valid"] and not request["command_checked"]),
            "command valid without command check")
    require(request["request_known"] == request["parsed"], "request-known does not match parsed")
    if request["request_known"]:
        require(request["recv"] == 176 and request["flags"] & (0x20 | 0x08) == 0,
                "parsed request has wrong frame size or truncated flags")
        require(request["operation"] in (1, 2) and request["stage"] <= 5 and
                request["job"] > 0 and request["attempt"] > 0,
                "parsed request fields are out of protocol range")
        require(request["signal_checked"] ==
                (request["state_valid"] and request["operation"] == 2),
                "signal-check state differs from operation and state")
        require(request["command_checked"] ==
                (request["state_valid"] and (request["operation"] != 2 or request["signal_valid"])),
                "command-check state differs from preceding validation")
    else:
        require(request["operation"] == request["stage"] == request["job"] == request["attempt"] == 0,
                "unknown request exposes nonzero request values")
        require(not request["state_checked"] and not request["signal_checked"] and
                not request["command_checked"], "unknown request has downstream checks")
    # MSG_EOR may be reported by SOCK_SEQPACKET; every other unknown flag fails closed.
    require(request["flags"] & ~(0x20 | 0x08 | 0x80) == 0,
            "unknown recvmsg flag")


def _parse_stat(raw: bytes, expected_pid: int, expected_ticks: int) -> dict[str, Any]:
    require(raw.endswith(b"\n") and b"\x00" not in raw, "invalid proc stat terminator")
    try:
        text = raw.decode("ascii", "strict").rstrip("\n")
    except UnicodeDecodeError as exc:
        raise EvidenceError("proc stat is not ASCII") from exc
    close = text.rfind(")")
    require(close > 0 and close + 2 < len(text) and text[close + 1] == " ",
            "malformed proc stat comm")
    pid_text = text[:text.find(" ")]
    pid = canonical_uint(pid_text, "stat PID", (1 << 31) - 1, False)
    tail = text[close + 2:].split()
    require(len(tail) >= 20 and len(tail[0]) == 1, "truncated proc stat fields")
    ticks = canonical_uint(tail[19], "stat starttime", (1 << 64) - 1, False)
    require(pid == expected_pid and ticks == expected_ticks, "proc stat PID/starttime mismatch")
    return {"pid": pid, "state": tail[0], "start_ticks": ticks}


def _parse_status(raw: bytes) -> dict[str, str]:
    require(raw.endswith(b"\n") and b"\x00" not in raw, "invalid proc status terminator")
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as exc:
        raise EvidenceError("proc status is not ASCII") from exc
    result: dict[str, str] = {}
    for line in lines:
        require(":" in line, "malformed proc status field")
        key, value = line.split(":", 1)
        require(bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", key)) and key not in result,
                "invalid or duplicate proc status field")
        result[key] = value.strip()
    required = {"Pid", "Uid", "Gid", "Groups", "CapInh", "CapPrm", "CapEff", "CapBnd",
                "CapAmb", "NoNewPrivs", "Seccomp", "Seccomp_filters"}
    require(required <= result.keys(), "proc status is missing required security fields")
    return result


def _parse_exe(raw: bytes) -> dict[str, Any]:
    try:
        text = raw.decode("ascii", "strict")
    except UnicodeDecodeError as exc:
        raise EvidenceError("exe diagnostic is not ASCII") from exc
    match = re.fullmatch(r"path=(/[^\s]+) dev=(0|[1-9][0-9]*) ino=(0|[1-9][0-9]*) "
                         r"mode=([0-7]+) size=(0|[1-9][0-9]*)", text)
    require(match is not None, "malformed exe diagnostic")
    result = {"path": match.group(1), "dev": int(match.group(2)), "ino": int(match.group(3)),
              "mode": int(match.group(4), 8), "size": int(match.group(5))}
    require(result["ino"] > 0 and result["size"] > 0 and stat.S_ISREG(result["mode"]) and
            result["mode"] & 0o111 != 0, "exe diagnostic is not a regular executable")
    return result


def _parse_socket(raw: bytes) -> dict[str, Any]:
    try:
        text = raw.decode("ascii", "strict")
    except UnicodeDecodeError as exc:
        raise EvidenceError("socket diagnostic is not ASCII") from exc
    match = re.fullmatch(r"dev=(0|[1-9][0-9]*) ino=(0|[1-9][0-9]*) mode=([0-7]+)", text)
    require(match is not None, "malformed socket diagnostic")
    result = {"dev": int(match.group(1)), "ino": int(match.group(2)),
              "mode": int(match.group(3), 8)}
    require(result["ino"] > 0 and stat.S_ISSOCK(result["mode"]),
            "socket diagnostic is not a socket inode")
    return result


def parse_sequence(messages: list[str]) -> dict[str, Any]:
    require(4 <= len(messages) <= 2048, "diagnostic sequence line count is out of bounds")
    total_bytes = 0
    parsed = [_parse_message(message) for message in messages]
    for message in messages:
        size = len(message.encode("ascii", "strict")) + 1
        require(size < MAX_DIAG_LINE, "diagnostic line reaches producer bound")
        total_bytes += size
    require(total_bytes <= MAX_DIAG_BYTES, "diagnostic sequence reaches output bound")
    require(parsed[0][0] == "BEGIN", "diagnostic sequence does not start with BEGIN")
    begin = parsed[0][1]
    pid, ticks = begin["pid"], begin["ticks"]
    pos = 1
    fields: dict[str, bytearray] = {}
    totals: dict[str, int] = {}
    next_field = 0
    while pos < len(parsed) and parsed[pos][0] == "DATA":
        record = parsed[pos][1]
        require(record["pid"] == pid and record["ticks"] == ticks,
                "DATA identity differs from BEGIN")
        field = record["field"]
        require(next_field < len(DATA_FIELDS) and field == DATA_FIELDS[next_field],
                "DATA fields are missing, duplicated, or reordered")
        if field not in fields:
            require(record["offset"] == 0 and field not in totals, "first DATA offset is not zero")
            fields[field] = bytearray()
            totals[field] = record["total"]
        require(record["total"] == totals[field] and record["offset"] == len(fields[field]),
                "DATA chunk total or offset is discontinuous")
        raw_chunk = bytes.fromhex(record["hex"])
        expected_chunk = min(512, record["total"] - record["offset"])
        require(expected_chunk > 0 and len(raw_chunk) == expected_chunk,
                "DATA chunk length differs from producer chunk size")
        fields[field].extend(raw_chunk)
        if len(fields[field]) == totals[field]:
            require(len(fields[field]) <= DATA_LIMITS[field], "DATA field exceeds producer read bound")
            next_field += 1
        else:
            require(len(fields[field]) < totals[field], "DATA chunk exceeds declared total")
        pos += 1
    require(next_field == len(DATA_FIELDS) and set(fields) == set(DATA_FIELDS),
            "diagnostic sequence is missing a DATA field")
    require(pos < len(parsed) and parsed[pos][0] == "SNAPSHOT_END",
            "diagnostic sequence lacks SNAPSHOT_END")
    end = parsed[pos][1]
    require(end["pid"] == pid and end["ticks"] == ticks and end["elapsed_ms"] <= 475,
            "SNAPSHOT_END identity or reserved-budget check failed")
    for field in DATA_FIELDS:
        require(end[field] == len(fields[field]) == totals[field],
                f"SNAPSHOT_END {field} size differs from DATA")
    pos += 1
    require(pos < len(parsed) and parsed[pos][0] == "REQUEST", "diagnostic sequence lacks REQUEST")
    request = parsed[pos][1]
    require(request["pid"] == pid and request["ticks"] == ticks,
            "REQUEST identity differs from BEGIN")
    _check_request_semantics(request)
    pos += 1
    require(pos < len(parsed) and parsed[pos][0] == "OUTCOME", "diagnostic sequence lacks OUTCOME")
    outcome = parsed[pos][1]
    require(outcome["pid"] == pid and outcome["ticks"] == ticks,
            "OUTCOME identity differs from BEGIN")
    pos += 1
    require(pos == len(parsed), "diagnostic sequence has extra records")
    stat_identity = _parse_stat(bytes(fields["stat"]), pid, ticks)
    status = _parse_status(bytes(fields["status"]))
    require(canonical_uint(status["Pid"], "status PID", (1 << 31) - 1, False) == pid,
            "status PID differs from diagnostic identity")
    executable = _parse_exe(bytes(fields["exe"]))
    socket = _parse_socket(bytes(fields["socket"]))
    require(outcome["frame_sent"] in (0, 1), "invalid frame_sent")
    return {"pid": pid, "start_ticks": ticks, "stat": stat_identity,
            "status": status, "mountinfo": bytes(fields["mountinfo"]),
            "cgroup": bytes(fields["cgroup"]), "exe": executable, "socket": socket,
            "request": request, "outcome": outcome, "elapsed_ms": end["elapsed_ms"],
            "line_count": len(messages), "output_bytes": total_bytes}


def _field_text(record: dict[str, Any], key: str, label: str) -> str:
    value = record.get(key)
    require(isinstance(value, str) and "\x00" not in value and "\n" not in value and "\r" not in value,
            f"{label} is missing or not a scalar string")
    require(len(value) <= 8192, f"{label} exceeds field bound")
    return value


def collect_diagnostics(records: list[dict[str, Any]], expected_boot: str) -> dict[tuple[str, str, str, int], dict[str, Any]]:
    grouped: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    for record_number, record in enumerate(records, 1):
        message = record.get("MESSAGE")
        if not isinstance(message, str) or "BQ-BROKER-DIAG-V1" not in message:
            continue
        require(message.startswith("BQ-BROKER-DIAG-V1 "),
                f"journal record {record_number} embeds diagnostic marker in unrelated text")
        require("_LINE_BREAK" not in record,
                f"journal record {record_number} has stream line-break metadata")
        require(_field_text(record, "_TRANSPORT", "diagnostic transport") == "stdout",
                f"journal record {record_number} is not stdout transport")
        boot = _field_text(record, "_BOOT_ID", "diagnostic boot ID")
        require(HEX32.fullmatch(boot) is not None and boot == expected_boot,
                f"journal record {record_number} has wrong or malformed boot ID")
        unit = _field_text(record, "_SYSTEMD_UNIT", "diagnostic unit")
        unit_match = parse_broker_unit(unit, f"journal record {record_number} unit")
        invocation = _field_text(record, "_SYSTEMD_INVOCATION_ID", "diagnostic invocation ID")
        require(HEX32.fullmatch(invocation) is not None,
                f"journal record {record_number} has malformed invocation ID")
        pid = canonical_uint(_field_text(record, "_PID", "diagnostic PID"),
                             "trusted diagnostic PID", (1 << 31) - 1, False)
        stream = _field_text(record, "_STREAM_ID", "diagnostic stream ID")
        require(HEX32.fullmatch(stream) is not None,
                f"journal record {record_number} has malformed stream ID")
        kind, parsed_line = _parse_message(message)
        require(parsed_line["pid"] == pid,
                f"journal record {record_number} trusted PID differs from message PID")
        key = (boot, unit, invocation, pid)
        entry = grouped.setdefault(key, {"stream_id": stream, "messages": [], "message_kinds": [],
                                         "peer_pid": int(unit_match.group("peer_pid")),
                                         "peer_uid": int(unit_match.group("peer_uid")),
                                         "counter": int(unit_match.group("counter")),
                                         "record_numbers": []})
        require(entry["stream_id"] == stream,
                f"journal stream ID changed within invocation {unit}")
        entry["messages"].append(message)
        entry["message_kinds"].append(kind)
        entry["record_numbers"].append(record_number)
        require(len(entry["messages"]) <= 2048, "diagnostic instance line count exceeds bound")
        require(len(grouped) <= MAX_INSTANCES, "broker diagnostic instance bound exceeded")
    output: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    for key, entry in grouped.items():
        sequence = parse_sequence(entry["messages"])
        require(sequence["pid"] == key[3], "parsed sequence PID differs from trusted key")
        request = sequence["request"]
        require(request["peer_known"] and request["peer_pid"] == entry["peer_pid"] and
                request["peer_uid"] == entry["peer_uid"],
                f"peer credentials differ from socket-activated unit identity: {key[1]}")
        entry["sequence"] = sequence
        output[key] = entry
    return output


def collect_manager_start_rows(records: list[dict[str, Any]], expected_boot: str) -> dict[tuple[str, str], dict[str, Any]]:
    rows: dict[tuple[str, str], dict[str, Any]] = {}
    progress: dict[tuple[str, str], int] = {}
    invocations: dict[tuple[str, str], str] = {}
    for record_number, record in enumerate(records, 1):
        unit_value = record.get("UNIT")
        if not isinstance(unit_value, str) or not unit_value.startswith("buster-bench-systemd-broker@"):
            continue
        parse_broker_unit(unit_value, f"manager record {record_number} UNIT")
        message_id_value = record.get("MESSAGE_ID")
        if record.get("JOB_TYPE") != "start" and message_id_value not in (
                STARTING_MESSAGE_ID, STARTED_MESSAGE_ID, FAILED_MESSAGE_ID):
            continue
        require(record.get("JOB_TYPE") == "start",
                f"broker start-result record {record_number} lacks JOB_TYPE=start")
        require(_field_text(record, "_PID", "manager PID") == "1" and
                _field_text(record, "_TRANSPORT", "manager transport") == "journal",
                f"broker start record {record_number} is not from PID 1 journal transport")
        boot = _field_text(record, "_BOOT_ID", "manager boot ID")
        require(HEX32.fullmatch(boot) is not None and boot == expected_boot,
                f"broker start record {record_number} has wrong boot ID")
        message_id = _field_text(record, "MESSAGE_ID", "manager start message ID")
        key = (boot, unit_value)
        invocation = record.get("INVOCATION_ID")
        if invocation is not None:
            require(isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None and
                    invocation != "0" * 32,
                    f"manager start record {record_number} has malformed invocation ID")
            prior_invocation = invocations.get(key)
            require(prior_invocation is None or prior_invocation == invocation,
                    f"manager invocation changed for {unit_value}")
            invocations[key] = invocation
        if message_id == STARTING_MESSAGE_ID:
            require(record.get("JOB_RESULT") in (None, ""),
                    f"manager Starting record {record_number} has an unexpected result")
            progress[key] = progress.get(key, 0) + 1
            require(progress[key] == 1, f"duplicate manager Starting record for {unit_value}")
            continue
        if message_id == STARTED_MESSAGE_ID:
            result = "done"
            require(record.get("JOB_RESULT") == result,
                    f"manager Started record {record_number} is malformed")
        elif message_id == FAILED_MESSAGE_ID:
            result = "failed"
            require(record.get("JOB_RESULT") == result,
                    f"manager Failed record {record_number} is malformed")
        else:
            raise EvidenceError(f"unknown manager start-result message ID for {unit_value}")
        require(progress.get(key) == 1,
                f"manager terminal start record precedes or lacks Starting for {unit_value}")
        require(key not in rows, f"duplicate terminal manager start record for {unit_value}")
        rows[key] = {"boot_id": boot, "unit": unit_value, "result": result,
                     "invocation_id": invocation, "message_id": message_id,
                     "record_number": record_number}
    require(set(progress) == set(rows), "manager start records lack a matching Starting or terminal result")
    return rows


def collect_manager_failed_exit_rows(
        records: list[dict[str, Any]], expected_boot: str
        ) -> dict[tuple[str, str, str], dict[str, Any]]:
    """Index observed PID1 failures; this does not classify them as success."""
    rows: dict[tuple[str, str, str], dict[str, Any]] = {}
    starts: dict[tuple[str, str, str], tuple[int, Any]] = {}
    for number, record in enumerate(records, 1):
        unit = record.get("UNIT")
        message_id = record.get("MESSAGE_ID")
        if (not isinstance(unit, str) or not unit.startswith("buster-bench-systemd-broker@") or
                message_id not in (STARTED_MESSAGE_ID, PROCESS_EXIT_MESSAGE_ID, UNIT_FAILED_MESSAGE_ID)):
            continue
        parse_broker_unit(unit, "PID1 failure unit")
        require(record.get("_PID") == "1" and record.get("_TRANSPORT") == "journal" and
                record.get("_BOOT_ID") == expected_boot,
                "broker failure is not from the exact PID1/boot journal")
        invocation = _field_text(record, "INVOCATION_ID", "PID1 failure invocation")
        require(HEX32.fullmatch(invocation) is not None and invocation != "0" * 32,
                "PID1 failure invocation is malformed")
        key = (expected_boot, unit, invocation)
        if message_id == STARTED_MESSAGE_ID:
            require(key not in starts and record.get("JOB_TYPE") == "start" and record.get("JOB_RESULT") == "done",
                    "duplicate or malformed PID1 successful start before failure")
            starts[key] = (number, record.get("__MONOTONIC_TIMESTAMP"))
            continue
        timestamp = canonical_uint(_field_text(record, "__MONOTONIC_TIMESTAMP", "PID1 failure time"),
                                   "PID1 failure time", (1 << 64) - 1, False)
        row = rows.setdefault(key, {})
        require(len(rows) <= MAX_INSTANCES, "PID1 failure population exceeds bound")
        if message_id == PROCESS_EXIT_MESSAGE_ID:
            require("exit" not in row and "failure" not in row and
                    record.get("COMMAND") == "ExecStart",
                    "duplicate, reordered or non-main broker process exit")
            row["exit"] = {"record_number": number, "monotonic_us": timestamp,
                           "code": _field_text(record, "EXIT_CODE", "PID1 exit code"),
                           "status": _field_text(record, "EXIT_STATUS", "PID1 exit status")}
        else:
            require("exit" in row and "failure" not in row and
                    timestamp >= row["exit"]["monotonic_us"],
                    "PID1 unit failure lacks a unique preceding process exit")
            row["failure"] = {"record_number": number, "monotonic_us": timestamp,
                              "result": _field_text(record, "UNIT_RESULT", "PID1 unit result")}
    for key, row in rows.items():
        require(set(row) == {"exit", "failure"}, f"incomplete PID1 failure pair for {key[1]}")
        require(key in starts and starts[key][0] < row["exit"]["record_number"],
                "PID1 failure lacks its preceding exact successful start")
        row["start_record_number"] = starts[key][0]
        row["start_monotonic_us"] = canonical_uint(starts[key][1], "PID1 successful start time", (1 << 64) - 1, False)
        require(row["start_monotonic_us"] <= row["exit"]["monotonic_us"],
                "PID1 exit timestamp precedes successful start")
    return rows


def _request_tuple(request: dict[str, Any]) -> tuple[int, int, int, int]:
    return (request["operation"], request["stage"], request["job"], request["attempt"])


def validate_diagnostic_request_set(
        diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
        expected: dict[str, Any]) -> dict[str, int]:
    allowed = {(row["operation"], row["stage"], row["job"], row["attempt"])
               for row in expected["allowed_requests"]}
    request_count = 0
    root_rejection_count = 0
    for key, entry in diagnostics.items():
        request = entry["sequence"]["request"]
        outcome = entry["sequence"]["outcome"]
        require(request["peer_known"] and request["peer_pid"] == entry["peer_pid"] and
                request["peer_uid"] == entry["peer_uid"],
                f"trusted peer identity differs from unit suffix for {key[1]}")
        if not request["request_known"]:
            require(request["peer_uid"] == 0 and request["peer_pid"] > 0 and
                    not request["poll_called"] and not request["recv_called"] and
                    request["recv"] == -1 and request["flags"] == 0 and
                    outcome == {"pid": key[3], "ticks": entry["sequence"]["start_ticks"],
                               "broker_exit": 1, "frame_status": 126, "frame_sent": 1},
                    f"unparsed broker request is not the bounded root-peer denial for {key[1]}")
            root_rejection_count += 1
            continue
        require(request["peer_uid"] == 65000 and request["peer_uid"] in expected["profile"]["client_uids"],
                f"parsed request peer is not the exact service client for {key[1]}")
        tuple_key = _request_tuple(request)
        require(tuple_key in allowed,
                f"observed request tuple is outside caller allowlist for {key[1]}")
        request_count += 1
        checks_valid = request["command_valid"]
        if not checks_valid:
            require(outcome["broker_exit"] == 1 and outcome["frame_status"] == 126 and
                    outcome["frame_sent"] == 1,
                    f"rejected parsed request did not emit exact failure frame for {key[1]}")
        else:
            require(outcome["frame_sent"] == 1 and
                    outcome["broker_exit"] == (1 if outcome["frame_status"] == 126 else 0),
                    f"executed request has incoherent status frame/result for {key[1]}")
    require(request_count > 0, "no parsed service-client broker request was observed")
    require(root_rejection_count > 0,
            "fixed-scope C21 evidence lacks the expected unparsed root-peer rejection")
    return {"diagnostic_processes": len(diagnostics), "parsed_requests": request_count,
            "root_peer_rejections": root_rejection_count}


def _status_identity(status: dict[str, str], pid: int, profile: dict[str, Any], label: str) -> dict[str, Any]:
    require(canonical_uint(status["Pid"], f"{label} status PID", (1 << 31) - 1, False) == pid,
            f"{label} status PID mismatch")
    expected_uid = [profile["broker_uid"]] * 4
    expected_gid = [profile["broker_gid"]] * 4
    uid = [canonical_uint(part, f"{label} Uid", (1 << 32) - 1)
           for part in status["Uid"].split()]
    gid = [canonical_uint(part, f"{label} Gid", (1 << 32) - 1)
           for part in status["Gid"].split()]
    groups = [canonical_uint(part, f"{label} Groups", (1 << 32) - 1)
              for part in status["Groups"].split()]
    require(uid == expected_uid and gid == expected_gid and groups == profile["groups"] and
            groups == sorted(set(groups)),
            f"{label} credentials differ from exact profile IDs")
    for key, expected in profile["capabilities"].items():
        require(status[key] == expected, f"{label} {key} differs from profile")
    nnp = canonical_uint(status["NoNewPrivs"], f"{label} NoNewPrivs", 1)
    seccomp = canonical_uint(status["Seccomp"], f"{label} Seccomp", 2)
    filters = canonical_uint(status["Seccomp_filters"], f"{label} Seccomp_filters", 1_000_000)
    require(nnp == profile["no_new_privs"] and seccomp == profile["seccomp"] and
            filters >= profile["seccomp_filters_min"],
            f"{label} NNP/seccomp state differs from profile")
    return {"pid": pid, "uid": uid, "gid": gid, "groups": groups,
            "capabilities": {key: status[key] for key in profile["capabilities"]},
            "no_new_privs": nnp, "seccomp": seccomp, "seccomp_filters": filters}


def _unescape_mount_path(value: str) -> str:
    allowed = {"040": " ", "011": "\t", "012": "\n", "134": "\\"}
    result: list[str] = []
    index = 0
    while index < len(value):
        if value[index] != "\\":
            result.append(value[index])
            index += 1
            continue
        require(index + 4 <= len(value) and value[index + 1:index + 4] in allowed,
                "mountinfo path contains an invalid octal escape")
        result.append(allowed[value[index + 1:index + 4]])
        index += 4
    result_text = "".join(result)
    require(result_text.startswith("/") and "\x00" not in result_text and
            "//" not in result_text and "." not in result_text.split("/") and
            ".." not in result_text.split("/"), "mountinfo path is not canonical")
    return result_text


def _readonly_mounts(raw: bytes, targets: list[str], label: str) -> dict[str, bool]:
    require(raw.endswith(b"\n") and b"\x00" not in raw, f"{label} mountinfo terminator invalid")
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label} mountinfo is not ASCII") from exc
    mounts: dict[str, list[tuple[int, int, set[str]]]] = {}
    mount_ids: set[int] = set()
    for line in lines:
        columns = line.split()
        separator = columns.index("-") if "-" in columns else -1
        require(separator >= 6 and len(columns) >= separator + 4,
                f"{label} mountinfo record is malformed")
        mount_id = canonical_uint(columns[0], f"{label} mount ID", (1 << 31) - 1, False)
        parent_id = canonical_uint(columns[1], f"{label} parent mount ID", (1 << 31) - 1, False)
        require(mount_id not in mount_ids, f"{label} duplicate mount ID")
        mount_ids.add(mount_id)
        mountpoint = _unescape_mount_path(columns[4])
        options = set(columns[5].split(","))
        require(("ro" in options) != ("rw" in options),
                f"{label} mountinfo has ambiguous read/write options")
        mounts.setdefault(mountpoint, []).append((mount_id, parent_id, options))
    result: dict[str, bool] = {}
    for target in targets:
        require(target.startswith("/") and ".." not in target.split("/"),
                f"{label} required mount target is malformed: {target}")
        matching_paths = [mountpoint for mountpoint in mounts
                          if mountpoint == "/" or target == mountpoint or
                          target.startswith(mountpoint.rstrip("/") + "/")]
        require(bool(matching_paths), f"{label} required mount target is absent: {target}")
        most_specific_length = max(len(mountpoint) for mountpoint in matching_paths)
        most_specific_paths = {mountpoint for mountpoint in matching_paths
                               if len(mountpoint) == most_specific_length}
        require(len(most_specific_paths) == 1,
                f"{label} has ambiguous covering mountpoints for {target}")
        active_path = next(iter(most_specific_paths))
        active_entries = mounts[active_path]
        require(len(active_entries) == 1,
                f"{label} has duplicate ambiguous covering mount for {target}")
        _active_id, _parent_id, active_options = active_entries[0]
        result[target] = "ro" in active_options
        require(result[target], f"{label} required mount target is writable: {target}")
    return result


def _parse_cgroup(raw: bytes, expected_prefix: str, label: str) -> str:
    require(raw.endswith(b"\n") and b"\x00" not in raw, f"{label} cgroup terminator invalid")
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label} cgroup is not ASCII") from exc
    paths = []
    for line in lines:
        fields = line.split(":", 2)
        if len(fields) == 3 and fields[0] == "0" and fields[1] == "":
            paths.append(fields[2])
    require(len(paths) == 1 and paths[0].startswith("/") and ".." not in paths[0].split("/"),
            f"{label} has ambiguous unified cgroup")
    require(paths[0] == expected_prefix.rstrip("/") or paths[0].startswith(expected_prefix),
            f"{label} cgroup differs from expected broker slice")
    return paths[0]


def _same_security_status(first: dict[str, str], second: dict[str, str]) -> bool:
    keys = ("Pid", "Uid", "Gid", "Groups", "CapInh", "CapPrm", "CapEff", "CapBnd",
            "CapAmb", "NoNewPrivs", "Seccomp", "Seccomp_filters")
    return all(first[key] == second[key] for key in keys)


def _safe_ref(root_fd: int, ref: Any, label: str, maximum: int,
              require_success: bool = True) -> bytes:
    require(isinstance(ref, dict) and set(ref) == {"path", "sha256", "bytes", "exit", "timed_out"},
            f"{label} reference has unknown or missing fields")
    require(isinstance(ref["path"], str) and isinstance(ref["sha256"], str) and
            HEX64.fullmatch(ref["sha256"]) is not None and type(ref["bytes"]) is int and
            0 <= ref["bytes"] <= maximum and
            (ref["exit"] is None or type(ref["exit"]) is int and -255 <= ref["exit"] <= 255) and
            type(ref["timed_out"]) is bool and
            (not require_success or (ref["exit"] == 0 and ref["timed_out"] is False)),
            f"{label} capture metadata is incomplete")
    raw = read_regular_at(root_fd, ref["path"], maximum, allow_empty=True)
    require(len(raw) == ref["bytes"] and hashlib.sha256(raw).hexdigest() == ref["sha256"],
            f"{label} capture byte count or SHA256 mismatch")
    return raw


def _parse_systemctl_show(raw: bytes, unit: str, invocation: str | None,
                          snapshot: dict[str, Any], expected: dict[str, Any]) -> dict[str, str]:
    require(raw and b"\x00" not in raw and raw.endswith(b"\n"),
            f"systemctl show output is empty or unterminated for {unit}")
    try:
        text = raw.decode("utf-8", "strict")
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"systemctl show output is not UTF-8 for {unit}") from exc
    properties: dict[str, str] = {}
    for line in text.splitlines():
        require("=" in line, f"malformed systemctl show property for {unit}")
        key, value = line.split("=", 1)
        require(bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", key)) and key not in properties,
                f"invalid or duplicate systemctl property for {unit}")
        properties[key] = value
    required = {"Id", "LoadState", "ActiveState", "InvocationID", "MainPID", "ExecMainPID", "ControlGroup",
                "ExecMainStartTimestampMonotonic", "ExecMainStatus", "Result", "SubState",
                "ProtectSystem", "ReadOnlyPaths"}
    require(required <= properties.keys(), f"systemctl show lacks required identity for {unit}")
    require(properties["Id"] == unit and properties["LoadState"] == "loaded",
            f"systemctl show unit/load state mismatch for {unit}")
    show_invocation = properties["InvocationID"] or None
    require(show_invocation is None or
            (HEX32.fullmatch(show_invocation) is not None and show_invocation != "0" * 32),
            f"systemctl show invocation ID malformed for {unit}")
    if invocation is not None:
        require(show_invocation == invocation, f"systemctl show invocation differs for {unit}")
    elif snapshot["process_started"]:
        require(show_invocation is not None, f"running process lacks manager invocation ID for {unit}")
    main_pid = canonical_uint(properties["MainPID"], f"{unit} MainPID", (1 << 31) - 1)
    exec_main_pid = canonical_uint(properties["ExecMainPID"], f"{unit} ExecMainPID", (1 << 31) - 1)
    require(main_pid == snapshot["main_pid"] and
            (main_pid == 0 or exec_main_pid == main_pid),
            f"systemctl show PID differs from observer snapshot for {unit}")
    control_group = properties["ControlGroup"]
    if control_group:
        require(control_group.startswith(expected["profile"]["cgroup_prefix"]) and
                ".." not in control_group.split("/"), f"systemctl show cgroup differs for {unit}")
    else:
        require(not snapshot["process_started"], f"running process has empty ControlGroup for {unit}")
    expected_read_only_paths = expected["profile"]["read_only_mount_targets"][1:]
    require(properties["ProtectSystem"] == "strict" and
            properties["ReadOnlyPaths"].split() == expected_read_only_paths,
            f"systemctl show read-only sandbox properties differ for {unit}")
    for key in ("result", "substate", "exec_main_status"):
        prop = {"result": "Result", "substate": "SubState", "exec_main_status": "ExecMainStatus"}[key]
        require(isinstance(snapshot.get(key), str) and snapshot[key] == properties[prop],
                f"systemctl show {prop} differs from snapshot for {unit}")
    require(isinstance(snapshot.get("active_state"), str) and
            snapshot["active_state"] == properties["ActiveState"],
            f"systemctl show ActiveState differs from snapshot for {unit}")
    require(re.fullmatch(r"0|[1-9][0-9]*", properties["ExecMainStartTimestampMonotonic"]) is not None,
            f"systemctl show monotonic start timestamp malformed for {unit}")
    exec_start = canonical_uint(properties["ExecMainStartTimestampMonotonic"],
                                f"{unit} ExecMainStartTimestampMonotonic", (1 << 64) - 1)
    snapshot_exec_start = snapshot.get("exec_main_start_timestamp_monotonic")
    require(type(snapshot_exec_start) is int and exec_start == snapshot_exec_start and
            (not snapshot["process_started"] or exec_start > 0),
            f"systemctl show execution timestamp differs from snapshot for {unit}")
    return properties


def _validate_snapshot_capture(snapshot: dict[str, Any], unit_row: dict[str, Any],
                               expected: dict[str, Any], observer_fd: int) -> dict[str, Any]:
    unit = snapshot["unit"]
    invocation = snapshot.get("invocation_id")
    require(snapshot["boot_id"] == expected["boot_id"] and
            snapshot["boot_id_raw"] == expected["boot_id_raw"] and
            unit == unit_row["unit"] and snapshot["object_path"] == unit_row["object_path"] and
            snapshot["generation"] == unit_row["generation"],
            f"snapshot lifecycle identity differs for {unit}")
    require(invocation is None or
            (isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None and
             invocation != "0" * 32),
            f"snapshot invocation ID is malformed for {unit}")
    main_pid = snapshot["main_pid"]
    started = snapshot["process_started"]
    start_ticks = snapshot.get("start_ticks")
    exec_start = snapshot.get("exec_main_start_timestamp_monotonic")
    require(type(main_pid) is int and 0 <= main_pid <= (1 << 31) - 1 and
            type(started) is bool and started == (main_pid > 0) and
            (start_ticks is None or type(start_ticks) is int and start_ticks > 0 and started) and
            (exec_start is None or type(exec_start) is int and exec_start >= 0),
            f"snapshot process identity types are malformed for {unit}")
    if started:
        require(isinstance(snapshot.get("cgroup"), str) and
                snapshot["cgroup"].startswith(expected["profile"]["cgroup_prefix"]) and
                ".." not in snapshot["cgroup"].split("/"),
                f"started process has a malformed cgroup for {unit}")
        if snapshot.get("manager_properties_complete") is True:
            require(invocation is not None and type(exec_start) is int and exec_start > 0,
                    f"manager-complete process identity lacks invocation or exec time for {unit}")
    props_complete = snapshot.get("manager_properties_complete", False)
    require(type(props_complete) is bool, f"manager completeness is malformed for {unit}")
    systemctl_exit = snapshot.get("systemctl_exit")
    timed_out = snapshot.get("systemctl_timed_out")
    cancelled = snapshot.get("systemctl_cancelled")
    require(type(timed_out) is bool and (cancelled is None or isinstance(cancelled, str)),
            f"systemctl completion metadata is malformed for {unit}")
    raw_show_ref = snapshot.get("raw_show")
    raw_stderr_ref = snapshot.get("raw_stderr")
    require((raw_show_ref is None) == (raw_stderr_ref is None),
            f"systemctl show/stderr raw references are unpaired for {unit}")
    properties: dict[str, str] | None = None
    stderr_raw = b""
    if raw_show_ref is not None:
        show_raw = _safe_ref(observer_fd, raw_show_ref, f"{unit} systemctl show", 4 * 1024 * 1024,
                             require_success=False)
        stderr_raw = _safe_ref(observer_fd, raw_stderr_ref, f"{unit} systemctl stderr", 1024 * 1024,
                               require_success=False)
        require(raw_show_ref["exit"] == raw_stderr_ref["exit"] == systemctl_exit and
                raw_show_ref["timed_out"] == raw_stderr_ref["timed_out"] == timed_out,
                f"systemctl raw references disagree with summary for {unit}")
        if props_complete:
            require(cancelled is None and systemctl_exit == 0 and not timed_out and not stderr_raw.strip(),
                    f"completed systemctl attempt has exit/timeout/cancel/stderr failure for {unit}")
            properties = _parse_systemctl_show(show_raw, unit, invocation, snapshot, expected)
        elif systemctl_exit == 0 and not timed_out and cancelled is None:
            try:
                properties = _parse_systemctl_show(show_raw, unit, invocation, snapshot, expected)
            except EvidenceError:
                properties = None
    else:
        require(not props_complete, f"manager-complete snapshot lacks systemctl raw references for {unit}")
    if properties is not None and snapshot.get("cgroup") is not None:
        require(properties["ControlGroup"] == snapshot["cgroup"],
                f"systemctl show cgroup differs from snapshot for {unit}")
    proc_refs = snapshot.get("proc_capture")
    require(proc_refs is None or isinstance(proc_refs, dict),
            f"process proc references are malformed for {unit}")
    proc: dict[str, Any] | None = None
    socket_identity = snapshot.get("socket_fd0")
    if proc_refs:
        allowed_proc_refs = {"proc-status", "proc-mountinfo", "proc-cgroup", "fd0_link", "fd0_info"}
        require(started and type(start_ticks) is int and set(proc_refs) <= allowed_proc_refs and
                ("fd0_link" in proc_refs) == ("fd0_info" in proc_refs),
                f"partial process capture references have invalid names or identity for {unit}")
        status_raw = (_safe_ref(observer_fd, proc_refs["proc-status"], f"{unit} proc status", 1024 * 1024)
                      if "proc-status" in proc_refs else None)
        mount_raw = (_safe_ref(observer_fd, proc_refs["proc-mountinfo"], f"{unit} proc mountinfo", 16 * 1024 * 1024)
                     if "proc-mountinfo" in proc_refs else None)
        cgroup_raw = (_safe_ref(observer_fd, proc_refs["proc-cgroup"], f"{unit} proc cgroup", 1024 * 1024)
                      if "proc-cgroup" in proc_refs else None)
        status = (_check_profile_status(status_raw, main_pid, expected["profile"], f"{unit} external")
                  if status_raw is not None else None)
        cgroup_path = (_parse_cgroup(cgroup_raw, expected["profile"]["cgroup_prefix"], f"{unit} external")
                       if cgroup_raw is not None else None)
        if cgroup_path is not None:
            require(cgroup_path == snapshot["cgroup"] and
                    snapshot.get("proc_cgroup_raw") in (None, f"0::{cgroup_path}"),
                    f"external cgroup does not match manager cgroup for {unit}")
        ro_mounts = (_readonly_mounts(mount_raw, expected["profile"]["read_only_mount_targets"],
                                     f"{unit} external") if mount_raw is not None else None)
        if "fd0_link" in proc_refs:
            require(socket_identity is not None,
                    f"partial fd0 files lack their socket identity for {unit}")
            _validate_fd0(snapshot, proc_refs, observer_fd, None, unit)
        else:
            require(socket_identity is None,
                    f"snapshot has fd0 identity without fd0 files for {unit}")
        proc = {"status_raw": status_raw, "status": status,
                "mountinfo_raw": mount_raw, "ro_mounts": ro_mounts,
                "cgroup_raw": cgroup_raw, "cgroup_path": cgroup_path}
    else:
        require(socket_identity is None,
                f"snapshot has fd0 identity without proc captures for {unit}")
    if snapshot["complete"]:
        require(props_complete and snapshot.get("process_capture_complete") is True and
                started and invocation is not None and type(start_ticks) is int and start_ticks > 0 and
                properties is not None and proc is not None and
                proc["status"] is not None and proc["mountinfo_raw"] is not None and
                proc["cgroup_path"] is not None and proc["ro_mounts"] is not None and
                socket_identity is not None and
                snapshot.get("exe") == expected["broker_executable_path"] and
                snapshot.get("exe_sha256") == expected["broker_binary_sha256"],
                f"complete process snapshot lacks manager/proc/exe/fd0 witnesses for {unit}")
    return {"complete": snapshot["complete"], "properties": properties,
            "main_pid": main_pid, "start_ticks": start_ticks,
            "process_identity": _snapshot_process_identity(snapshot),
            "proc": proc, "socket": socket_identity, "stderr": stderr_raw}


def _check_profile_status(raw: bytes, pid: int, profile: dict[str, Any], label: str) -> dict[str, str]:
    status = _parse_status(raw)
    _status_identity(status, pid, profile, label)
    return status


def _validate_fd0(snapshot: dict[str, Any], proc_refs: dict[str, Any], root_fd: int,
                  sequence: dict[str, Any] | None, label: str) -> None:
    socket_identity = snapshot.get("socket_fd0")
    require(isinstance(socket_identity, dict) and
            set(socket_identity) == {"fd", "target", "device", "inode", "mode", "fdinfo_inode",
                                     "start_ticks_before", "start_ticks_after", "stable"},
            f"{label} fd0 identity is missing or malformed")
    require(socket_identity["fd"] == 0 and socket_identity["stable"] is True and
            type(socket_identity["device"]) is int and socket_identity["device"] >= 0 and
            type(socket_identity["inode"]) is int and socket_identity["inode"] > 0 and
            type(socket_identity["mode"]) is int and
            stat.S_ISSOCK(socket_identity["mode"]) and
            socket_identity["fdinfo_inode"] == socket_identity["inode"] and
            socket_identity["start_ticks_before"] == snapshot["start_ticks"] ==
            socket_identity["start_ticks_after"],
            f"{label} fd0 identity changed during capture")
    require(socket_identity["target"] == f"socket:[{socket_identity['inode']}]",
            f"{label} fd0 readlink differs from inode")
    link_raw = _safe_ref(root_fd, proc_refs.get("fd0_link"), f"{label} fd0 link", 4096)
    info_raw = _safe_ref(root_fd, proc_refs.get("fd0_info"), f"{label} fd0 info", 65536)
    require(link_raw.endswith(b"\n") and link_raw[:-1].decode("ascii", "strict") == socket_identity["target"],
            f"{label} fd0 link artifact differs from summary")
    try:
        info_text = info_raw.decode("ascii", "strict")
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label} fdinfo is not ASCII") from exc
    infos = re.findall(r"^ino:\s+([0-9]+)$", info_text, re.MULTILINE)
    require(len(infos) == 1 and int(infos[0]) == socket_identity["inode"],
            f"{label} fdinfo inode differs from fd0 stat")
    if sequence is not None:
        require(sequence["socket"] == {"dev": socket_identity["device"],
                                        "ino": socket_identity["inode"],
                                        "mode": socket_identity["mode"]},
                f"{label} C-reported accepted socket differs from external fd0")


def _observer_documents(observer_fd: int, expected: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any]]:
    ready = strict_json(read_regular_at(observer_fd, "ready.json", 1024 * 1024), "observer ready.json")
    event_raw = read_regular_at(observer_fd, "events.jsonl", MAX_OBSERVER_EVENTS, allow_empty=True)
    events = parse_jsonl(event_raw, "observer events.jsonl") if event_raw else []
    summary = strict_json(read_regular_at(observer_fd, "summary.json", MAX_OBSERVER_SUMMARY),
                          "observer summary.json")
    require(isinstance(ready, dict) and ready.get("schema") == OBS_SCHEMA and ready.get("kind") == "READY",
            "observer readiness schema mismatch")
    require(isinstance(summary, dict), "observer summary is not an object")
    for document, label in ((ready, "ready"), (summary, "summary")):
        boot = document.get("boot_id")
        raw_boot = document.get("boot_id_raw")
        require(isinstance(boot, str) and HEX32.fullmatch(boot) is not None and
                isinstance(raw_boot, str) and boot_uuid(raw_boot, f"observer {label} boot_id_raw") == boot and
                boot == expected["boot_id"] and raw_boot == expected["boot_id_raw"],
                f"observer {label} boot identity mismatch")
    require(ready.get("run_id") == expected["run_id"] and
            ready.get("run_attempt") == expected["run_attempt"] and
            ready.get("match_ack") is True and ready.get("subscribe_ack") is True and
            ready.get("population_complete") is False and
            ready.get("manager_object_reload_possible") is True and
            type(ready.get("ready_monotonic_ns")) is int and ready["ready_monotonic_ns"] > 0 and
            type(ready.get("observer_pid")) is int and ready["observer_pid"] > 0 and
            type(ready.get("observer_start_ticks")) is int and ready["observer_start_ticks"] > 0 and
            isinstance(ready.get("manager_sender"), str) and
            re.fullmatch(r":[1-9][0-9]*\.(?:0|[1-9][0-9]*)", ready["manager_sender"]) is not None,
            "observer readiness is incomplete or bound to another run")
    require(summary.get("schema") == OBS_SCHEMA and summary.get("run_id") == expected["run_id"] and
            summary.get("run_attempt") == expected["run_attempt"] and
            summary.get("population_complete") is False and
            summary.get("manager_object_reload_possible") is True and
            type(summary.get("event_loss_detected")) is bool and
            type(summary.get("event_stream_complete")) is bool and
            summary["event_stream_complete"] == (not summary["event_loss_detected"]),
            "observer summary event-loss status or run identity is malformed")
    require(type(summary.get("run_attempt")) is int and
            type(summary.get("observer_pid")) is int and summary["observer_pid"] == ready["observer_pid"] and
            type(summary.get("observer_start_ticks")) is int and
            summary["observer_start_ticks"] == ready["observer_start_ticks"] and
            summary.get("manager_sender") == ready["manager_sender"] and
            type(summary.get("ready_monotonic_ns")) is int and
            summary["ready_monotonic_ns"] == ready["ready_monotonic_ns"] and
            type(summary.get("ended_monotonic_ns")) is int and
            summary["ended_monotonic_ns"] >= summary["ready_monotonic_ns"] and
            summary.get("stop_seen") is True and
            type(summary.get("event_count")) is int and summary["event_count"] == len(events) and
            type(summary.get("event_bytes")) is int and summary["event_bytes"] == len(event_raw) and
            type(summary.get("capture_bytes")) is int and
            0 <= summary["capture_bytes"] <= MAX_OBSERVER_EVENTS,
            "observer summary counters or stop/identity fields disagree")
    require(len(events) <= 512 and type(summary.get("bus_dispatch_count")) is int and
            0 <= summary["bus_dispatch_count"] <= 4096,
            "observer event or dispatch count exceeds bound")
    for number, event in enumerate(events, 1):
        require(event.get("schema") == OBS_SCHEMA and event.get("boot_id") == expected["boot_id"] and
                event.get("boot_id_raw") == expected["boot_id_raw"] and
                type(event.get("seq")) is int and event.get("seq") == number and
                type(event.get("monotonic_ns")) is int and event["monotonic_ns"] > 0,
                f"observer event {number} has a sequence or boot gap")
    return ready, events, summary


def _unit_object_path(unit: str) -> str:
    parse_broker_unit(unit, "observer lifecycle unit")
    escaped = "".join(chr(byte) if (48 <= byte <= 57 or 65 <= byte <= 90 or 97 <= byte <= 122)
                      else f"_{byte:02x}" for byte in unit.encode("ascii"))
    return "/org/freedesktop/systemd1/unit/" + escaped


def _generation_key(row: dict[str, Any]) -> tuple[str, str, str, int]:
    boot = row.get("boot_id")
    unit = row.get("unit")
    path = row.get("object_path")
    generation = row.get("generation")
    require(isinstance(boot, str) and HEX32.fullmatch(boot) is not None and
            isinstance(path, str) and len(path) <= 512 and
            path.startswith("/org/freedesktop/systemd1/unit/") and
            ".." not in path.split("/") and type(generation) is int and
            0 < generation <= (1 << 64) - 1,
            "observer lifecycle identity is malformed")
    parse_broker_unit(unit, "observer lifecycle unit")
    require(path == _unit_object_path(unit), "observer object path does not encode its exact unit")
    return boot, unit, path, generation


def _snapshot_process_identity(snapshot: dict[str, Any]) -> tuple[Any, ...] | None:
    invocation = snapshot.get("invocation_id")
    pid = snapshot.get("main_pid")
    exec_start = snapshot.get("exec_main_start_timestamp_monotonic")
    cgroup = snapshot.get("cgroup")
    ticks = snapshot.get("start_ticks")
    if (type(pid) is not int or pid <= 0 or not isinstance(invocation, str) or
            HEX32.fullmatch(invocation) is None or invocation == "0" * 32 or
            type(exec_start) is not int or exec_start <= 0 or
            not isinstance(cgroup, str) or not cgroup.startswith("/") or
            type(ticks) is not int or ticks <= 0):
        return None
    return (*_generation_key(snapshot), invocation, pid, exec_start, cgroup, ticks)


def _post_removal_unstarted(snapshot: dict[str, Any], row: dict[str, Any],
                           removed: dict[str, Any] | None) -> bool:
    if snapshot.get("post_removal_unstarted") is not True:
        return False
    require(removed is not None and
            all(type(snapshot.get(key)) is int for key in
                ("trigger_event_seq", "started_monotonic_ns", "finished_monotonic_ns")) and
            snapshot["trigger_event_seq"] < removed["seq"] and
            snapshot.get("started_monotonic_ns", 0) >= removed["monotonic_ns"] and
            snapshot.get("finished_monotonic_ns", 0) >= snapshot["started_monotonic_ns"] and
            snapshot.get("incomplete_reasons") == ["unit_removed_before_snapshot"] and
            snapshot.get("complete") is False and snapshot.get("manager_properties_complete") is False and
            snapshot.get("process_capture_complete") is False and
            type(snapshot.get("main_pid")) is int and snapshot.get("main_pid") == 0 and
            snapshot.get("process_started") is False and snapshot.get("proc_capture") == {} and
            snapshot.get("systemctl_timed_out") is False and
            all(snapshot.get(key) is None for key in ("raw_show", "raw_stderr", "systemctl_exit",
                "systemctl_timeout_reason", "systemctl_cancelled", "socket_fd0", "invocation_id",
                "start_ticks", "exe", "exe_sha256", "cgroup", "result", "active_state", "substate",
                "exec_main_status", "invocation_id_observed", "exec_main_start_timestamp_monotonic",
                "process_capture_state", "initial_exe_observed", "proc_cgroup_raw",
                "preliminary_property_miss", "preliminary_property_miss_reason")),
            "post-removal query was started, has payload, or disagrees with event order")
    return any(old.get("complete") is True and old.get("manager_properties_complete") is True and
               old.get("process_capture_complete") is True and
               _snapshot_process_identity(old) is not None and
               type(old.get("finished_monotonic_ns")) is int and
               old.get("finished_monotonic_ns", removed["monotonic_ns"] + 1) <= removed["monotonic_ns"] and
               _generation_key(old) == _generation_key(snapshot)
               for old in row["snapshots"])


def _retained_terminal_shape(row: dict[str, Any], summary: dict[str, Any],
                             life_events: list[dict[str, Any]],
                             removed: dict[str, Any] | None) -> bool:
    terminal = row.get("retained_failed_terminal")
    if terminal is None:
        return False
    require(isinstance(terminal, dict) and set(terminal) == TERMINAL_FIELDS and
            _generation_key(terminal) == _generation_key(row) and
            terminal.get("boot_id_raw") == row["boot_id_raw"] and
            terminal.get("manager_sender") == summary["manager_sender"] and
            terminal.get("manager_properties_complete") is True and
            terminal.get("incomplete_reasons") == [] and terminal.get("systemctl_exit") == 0 and
            type(terminal.get("systemctl_exit")) is int and
            terminal.get("systemctl_timed_out") is False and
            terminal.get("systemctl_timeout_reason") is None and
            terminal.get("systemctl_cancelled") is None,
            "retained-failed terminal identity or completed query is invalid")
    phase_keys = ("stop_monotonic_ns", "terminal_phase_started_monotonic_ns",
                  "terminal_phase_finished_monotonic_ns", "ready_monotonic_ns", "ended_monotonic_ns")
    require(all(type(summary.get(key)) is int and summary[key] > 0 for key in phase_keys) and
            summary["ready_monotonic_ns"] <= summary["stop_monotonic_ns"] <=
            summary["terminal_phase_started_monotonic_ns"] <= summary["terminal_phase_finished_monotonic_ns"] <=
            summary["ended_monotonic_ns"] and
            summary["terminal_phase_finished_monotonic_ns"] -
            summary["terminal_phase_started_monotonic_ns"] <= 30_000_000_000 and
            summary["terminal_phase_finished_monotonic_ns"] - summary["stop_monotonic_ns"] <= 30_000_000_000 and
            summary["terminal_phase_finished_monotonic_ns"] - summary["ready_monotonic_ns"] <= 4_000_000_000_000,
            "retained-failed terminal phase is missing, reordered or exceeds deadline")
    require(type(terminal.get("started_monotonic_ns")) is int and
            type(terminal.get("finished_monotonic_ns")) is int and
            summary["terminal_phase_started_monotonic_ns"] <= terminal["started_monotonic_ns"] <=
            terminal["finished_monotonic_ns"] <= summary["terminal_phase_finished_monotonic_ns"] and
            terminal["finished_monotonic_ns"] - terminal["started_monotonic_ns"] <= 2_000_000_000,
            "retained-failed query is pre-stop, reordered or exceeds query deadline")
    event_seq = terminal.get("event_seq")
    referenced = [event for event in life_events if event["seq"] == event_seq]
    require(type(event_seq) is int and len(referenced) == 1 and
            referenced[0]["monotonic_ns"] <= terminal["started_monotonic_ns"] and
            (removed is None and event_seq == row["last_event_seq"] or
             removed is not None and event_seq < removed["seq"] and
             terminal["finished_monotonic_ns"] <= removed["monotonic_ns"]),
            "retained terminal does not bind the final event horizon or predates removal incorrectly")
    properties = terminal.get("properties")
    require(isinstance(properties, dict) and set(properties) == TERMINAL_PROPERTIES and
            all(isinstance(value, str) and len(value) <= 4096 and "\x00" not in value
                for value in properties.values()), "retained terminal property map is malformed")
    fixed = {"Id": row["unit"], "LoadState": "loaded", "CollectMode": "inactive",
             "ActiveState": "failed", "SubState": "failed", "MainPID": "0", "Result": "exit-code",
             "ExecMainCode": "1", "ExecMainStatus": "1"}
    require(all(properties[key] == value for key, value in fixed.items()),
            "retained terminal does not describe the exact retained failed state")
    witnesses = {_snapshot_process_identity(snapshot) for snapshot in row["snapshots"]
                 if snapshot.get("complete") is True and snapshot.get("manager_properties_complete") is True and
                 snapshot.get("process_capture_complete") is True and
                 type(snapshot.get("finished_monotonic_ns")) is int and
                 snapshot.get("finished_monotonic_ns", terminal["started_monotonic_ns"] + 1) <=
                 terminal["started_monotonic_ns"]}
    require(len(witnesses) == 1 and None not in witnesses,
            "retained failure lacks one consistent earlier full live process identity")
    witness = next(iter(witnesses))
    exit_us = canonical_uint(properties["ExecMainExitTimestampMonotonic"], "retained exit time", (1 << 64) - 1, False)
    require(properties["InvocationID"] == witness[4] == row["invocation_id"] and
            properties["ExecMainPID"] == str(witness[5]) == str(row["main_pid"]) and
            properties["ExecMainStartTimestampMonotonic"] == str(witness[6]) and
            witness[6] < exit_us and exit_us * 1000 <= terminal["started_monotonic_ns"],
            "retained terminal process/start/exit differs from earlier full witness")
    return True


def _validate_template_metadata(ready: dict[str, Any], events: list[dict[str, Any]],
                                summary: dict[str, Any]) -> None:
    """Admit only the frozen pre-peer pair; journal exclusion is also mandatory."""
    rows = summary.get("template_metadata_events", [])
    require(isinstance(rows, list) and len(rows) in (0, 2),
            "template metadata must be absent or one complete pair")
    if rows:
        require(summary.get("event_stream_complete") is True and
                summary.get("event_loss_detected") is False and
                summary.get("global_incomplete_reasons") == [] and
                summary.get("stop_seen") is True,
                "template metadata lacks a complete continuous observer stream")
        ready_ns, ended_ns = ready.get("ready_monotonic_ns"), summary.get("ended_monotonic_ns")
        dispatches = summary.get("bus_dispatch_count")
        require(type(ready_ns) is int and ready_ns > 0 and
                type(ended_ns) is int and ended_ns >= ready_ns and
                type(dispatches) is int and 2 <= dispatches <= 4096,
                "template metadata lacks bounded observer clock/dispatch counters")
        first_new_ns = None
        for event in events:
            if event.get("event") == "UnitNew":
                parse_broker_unit(event.get("unit"), "first canonical broker event")
                first_new_ns = event.get("monotonic_ns")
                break
        require(type(first_new_ns) is int and ready_ns < first_new_ns <= ended_ns,
                "template metadata lacks a subsequent canonical broker UnitNew")
        last_ns, last_ordinal = ready_ns - 1, 0
        for row, member in zip(rows, ("UnitNew", "UnitRemoved")):
            require(isinstance(row, dict) and set(row) == TEMPLATE_METADATA_FIELDS,
                    "template metadata has unknown or missing fields")
            require(row["boot_id"] == ready.get("boot_id") and
                    row["boot_id_raw"] == ready.get("boot_id_raw") and
                    boot_uuid(row["boot_id_raw"], "template metadata boot") == row["boot_id"] and
                    row["unit"] == TEMPLATE_METADATA_UNIT and
                    row["object_path"] == TEMPLATE_METADATA_PATH and
                    row["sender"] == ready.get("manager_sender") and
                    row["member"] == member and
                    row["interface"] == "org.freedesktop.systemd1.Manager" and
                    row["path"] == "/org/freedesktop/systemd1" and row["signature"] == "so",
                    "template metadata identity/header/order mismatch")
            ns, ordinal = row["monotonic_ns"], row["bus_dispatch_ordinal"]
            require(type(ns) is int and ready_ns <= ns <= ended_ns and
                    last_ns < ns < first_new_ns and
                    type(ordinal) is int and last_ordinal < ordinal <= dispatches,
                    "template metadata clock or dispatch order is invalid")
            last_ns, last_ordinal = ns, ordinal


def _validate_event_rows(ready: dict[str, Any], events: list[dict[str, Any]],
                         summary: dict[str, Any]) -> tuple[dict[tuple[str, str, str, int], dict[str, Any]],
                                                          list[dict[str, Any]]]:
    _validate_template_metadata(ready, events, summary)
    ready_ns = ready["ready_monotonic_ns"]
    global_reasons = summary.get("global_incomplete_reasons")
    require(isinstance(global_reasons, list) and
            len(global_reasons) <= 64 and len(global_reasons) == len(set(global_reasons)) and
            all(isinstance(item, str) and item and len(item) <= 512 for item in global_reasons),
            "observer global incomplete reasons are malformed")
    lifecycles: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    last_ns = ready_ns
    manager_reloading_seen = False
    for event in events:
        require(boot_uuid(event.get("boot_id_raw"), "event boot_id_raw") == event["boot_id"],
                "observer event raw boot UUID differs")
        require(event["monotonic_ns"] >= last_ns, "observer monotonic event order regressed")
        last_ns = event["monotonic_ns"]
        require(event.get("sender") == ready["manager_sender"] and
                isinstance(event.get("member"), str), "observer event sender/member is missing")
        kind = event.get("event")
        if kind == "Reloading":
            expected_fields = {"schema", "boot_id", "boot_id_raw", "seq", "monotonic_ns",
                               "observer_show_query_inflight", "event", "member", "interface",
                               "path", "sender", "reloading"}
            require(set(event) == expected_fields,
                    "manager Reloading event has unknown or missing fields")
            require(event.get("member") == "Reloading" and
                    event.get("interface") == "org.freedesktop.systemd1.Manager" and
                    event.get("path") == "/org/freedesktop/systemd1" and
                    type(event.get("reloading")) is bool,
                    "malformed system manager Reloading event")
            require("manager_reloading_during_observation" in global_reasons,
                    "manager Reloading event lacks a global incomplete reason")
            manager_reloading_seen = True
            continue
        if kind in ("UnitNew", "UnitRemoved"):
            expected_fields = {"schema", "boot_id", "boot_id_raw", "seq", "monotonic_ns",
                               "observer_show_query_inflight", "event", "member", "interface",
                               "sender", "unit", "object_path", "generation"}
        elif kind == "PropertiesChanged":
            expected_fields = {"schema", "boot_id", "boot_id_raw", "seq", "monotonic_ns",
                               "observer_show_query_inflight", "event", "member", "interface",
                               "unit_interface", "sender", "unit", "object_path", "generation",
                               "changed_properties", "invalidated_properties", "main_pids"}
        else:
            raise EvidenceError("observer has an unknown lifecycle event")
        require(set(event) == expected_fields,
                "observer lifecycle event has unknown or missing fields")
        query_context = event.get("observer_show_query_inflight")
        require(query_context is None or
                (isinstance(query_context, dict) and
                 set(query_context) == {"unit", "generation", "started_monotonic_ns"} and
                 isinstance(query_context.get("unit"), str) and
                 type(query_context.get("generation")) is int and query_context["generation"] > 0 and
                 type(query_context.get("started_monotonic_ns")) is int and
                 query_context["started_monotonic_ns"] > 0),
                "observer in-flight query context is malformed")
        key = _generation_key(event)
        if kind == "UnitNew":
            require(event.get("member") == "UnitNew" and
                    event.get("interface") == "org.freedesktop.systemd1.Manager" and
                    key not in lifecycles, "duplicate or malformed UnitNew event")
            lifecycles[key] = {"new": event, "properties": [], "removed": None}
        elif kind == "PropertiesChanged":
            require(event.get("member") == "PropertiesChanged" and
                    event.get("interface") == "org.freedesktop.DBus.Properties" and
                    event.get("unit_interface") in ("org.freedesktop.systemd1.Unit",
                                                     "org.freedesktop.systemd1.Service") and
                    isinstance(event.get("changed_properties"), list) and
                    len(event["changed_properties"]) == len(set(event["changed_properties"])) and
                    all(isinstance(item, str) and item for item in event["changed_properties"]) and
                    isinstance(event.get("invalidated_properties"), list) and
                    all(isinstance(item, str) and item for item in event["invalidated_properties"]) and
                    len(event["invalidated_properties"]) == len(set(event["invalidated_properties"])) and
                    isinstance(event.get("main_pids"), list) and
                    all(type(item) is int and 0 <= item <= (1 << 31) - 1 for item in event["main_pids"]),
                    "malformed PropertiesChanged event")
            if "MainPID" in event["changed_properties"] or "MainPID" in event["invalidated_properties"]:
                require(event["unit_interface"] == "org.freedesktop.systemd1.Service",
                        "MainPID event is not from the systemd Service interface")
            require(key in lifecycles and lifecycles[key]["removed"] is None,
                    "PropertiesChanged is outside its observed generation")
            lifecycles[key]["properties"].append(event)
        elif kind == "UnitRemoved":
            require(event.get("member") == "UnitRemoved" and
                    event.get("interface") == "org.freedesktop.systemd1.Manager" and
                    key in lifecycles and lifecycles[key]["removed"] is None,
                    "duplicate or unpaired UnitRemoved event")
            lifecycles[key]["removed"] = event
    require(manager_reloading_seen ==
            ("manager_reloading_during_observation" in global_reasons),
            "manager Reloading event and global incomplete reason disagree")
    units = summary.get("units")
    require(isinstance(units, list) and len(units) <= MAX_INSTANCES,
            "observer summary units/snapshots missing or oversized")
    unit_rows: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    nested_snapshots: list[dict[str, Any]] = []
    for unit_row in units:
        require(isinstance(unit_row, dict), "observer unit row is not an object")
        key = _generation_key(unit_row)
        require(key not in unit_rows, "observer summary overwrote or duplicated a generation")
        boot_raw = unit_row.get("boot_id_raw")
        require(isinstance(boot_raw, str) and boot_uuid(boot_raw, "unit row boot_id_raw") == key[0],
                "observer unit raw boot ID differs")
        require(key in lifecycles, "observer summary unit has no manager lifecycle events")
        life = lifecycles[key]
        all_events = [life["new"], *life["properties"]]
        if life["removed"] is not None:
            all_events.append(life["removed"])
        all_events.sort(key=lambda row: row["seq"])
        removed = life["removed"]
        require(unit_row.get("first_event_seq") == all_events[0]["seq"] and
                unit_row.get("last_event_seq") == all_events[-1]["seq"] and
                unit_row.get("first_event_monotonic_ns") == all_events[0]["monotonic_ns"] and
                unit_row.get("last_event_monotonic_ns") == all_events[-1]["monotonic_ns"] and
                unit_row.get("removed_event_seq") == (removed["seq"] if removed else None) and
                unit_row.get("removed_monotonic_ns") == (removed["monotonic_ns"] if removed else None),
                f"observer lifecycle bounds disagree for {key[1]}")
        invocation = unit_row.get("invocation_id")
        require(invocation is None or
                (isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None and
                 invocation != "0" * 32),
                f"observer invocation ID is malformed for {key[1]}")
        main_pid = unit_row.get("main_pid")
        started = unit_row.get("process_started")
        ticks = unit_row.get("start_ticks")
        require(type(main_pid) is int and 0 <= main_pid <= (1 << 31) - 1 and
                type(started) is bool and (main_pid == 0 or started) and
                (ticks is None or (type(ticks) is int and ticks > 0 and main_pid > 0 and
                                   started and invocation is not None)),
                f"observer process identity is malformed for {key[1]}")
        require(type(unit_row.get("complete")) is bool and
                type(unit_row.get("manager_lifecycle_complete")) is bool and
                type(unit_row.get("process_complete")) is bool and
                unit_row["complete"] == unit_row["process_complete"] and
                type(unit_row.get("process_start_seen")) is bool and
                (not started or unit_row["process_start_seen"]) and
                isinstance(unit_row.get("incomplete_reasons"), list) and
                all(isinstance(item, str) and item for item in unit_row["incomplete_reasons"]) and
                isinstance(unit_row.get("manager_incomplete_reasons"), list) and
                all(isinstance(item, str) and item for item in unit_row["manager_incomplete_reasons"]) and
                isinstance(unit_row.get("process_incomplete_reasons"), list) and
                all(isinstance(item, str) and item for item in unit_row["process_incomplete_reasons"]) and
                isinstance(unit_row.get("preliminary_property_misses"), list) and
                len(unit_row["preliminary_property_misses"]) <= 6,
                f"observer unit completeness is malformed for {key[1]}")
        row_snaps = unit_row.get("snapshots")
        require(isinstance(row_snaps, list) and 1 <= len(row_snaps) <= 6,
                f"observer unit snapshot list missing or out of bounds for {key[1]}")
        has_terminal = unit_row.get("retained_failed_terminal") is not None
        require(len(row_snaps) + int(has_terminal) <= 6,
                f"observer terminal query exceeds the shared six-query bound for {key[1]}")
        for snapshot in row_snaps:
            require(isinstance(snapshot, dict) and _generation_key(snapshot) == key,
                    f"nested observer snapshot identity mismatch for {key[1]}")
            require(isinstance(snapshot.get("boot_id_raw"), str) and
                    boot_uuid(snapshot["boot_id_raw"], "snapshot boot_id_raw") == key[0],
                    f"snapshot raw boot ID differs for {key[1]}")
            require(snapshot.get("manager_object_reload_possible") is True,
                    f"snapshot lost manager-object reload caveat for {key[1]}")
            nested_snapshots.append(snapshot)
        manager_reasons = unit_row["manager_incomplete_reasons"]
        process_reasons = unit_row["process_incomplete_reasons"]
        event_loss = summary.get("event_loss_detected") is True
        terminal_complete = _retained_terminal_shape(unit_row, summary, all_events, removed)
        expected_unit_manager_complete = (
            (life["removed"] is not None or terminal_complete) and bool(row_snaps) and
            not manager_reasons and not event_loss and
            any(snapshot.get("manager_properties_complete") is True for snapshot in row_snaps) and
            all(snapshot.get("manager_properties_complete", False) is True or
                snapshot.get("preliminary_property_miss", False) is True or
                _post_removal_unstarted(snapshot, unit_row, removed) for snapshot in row_snaps))
        require(unit_row["manager_lifecycle_complete"] == expected_unit_manager_complete,
                f"observer manager lifecycle completeness is inconsistent for {key[1]}")
        positive_snapshots = [snapshot for snapshot in row_snaps
                              if type(snapshot.get("main_pid")) is int and snapshot["main_pid"] > 0]
        positive_event_pids = {pid for event in life["properties"]
                               for pid in event["main_pids"] if pid > 0}
        positive_event_pid = bool(positive_event_pids)
        witness_identities = {_snapshot_process_identity(snapshot) for snapshot in positive_snapshots
                              if snapshot.get("complete") is True and
                              _snapshot_process_identity(snapshot) is not None}
        require(unit_row["process_start_seen"] == (unit_row["process_started"] or positive_event_pid),
                f"observer process-start event summary differs for {key[1]}")
        witness_pid = next(iter(witness_identities))[5] if len(witness_identities) == 1 else None
        positive_identities_match = (len(witness_identities) == 1 and
            all(_snapshot_process_identity(snapshot) in witness_identities
                for snapshot in positive_snapshots) and
            all(snapshot.get("complete") is True or
                (snapshot.get("process_capture_state") in SUPERSEDABLE_CAPTURE_STATES and
                 snapshot.get("manager_properties_complete") is True and
                 snapshot.get("process_capture_complete") is False)
                for snapshot in positive_snapshots) and
            not any(pid != witness_pid for pid in positive_event_pids))
        expected_unit_process_complete = (expected_unit_manager_complete and
                                          positive_identities_match and not process_reasons)
        require(unit_row["process_complete"] ==
                expected_unit_process_complete,
                f"observer positive PID evidence conflicts with process witness for {key[1]}")
        unit_rows[key] = unit_row
    require(set(unit_rows) == set(lifecycles),
            "observer summary omitted or invented a manager lifecycle generation")
    require(len(nested_snapshots) + sum(row.get("retained_failed_terminal") is not None
                                      for row in unit_rows.values()) <= 768,
            "observer ordinary and terminal query count exceeds bound")
    for snapshot in nested_snapshots:
        require(isinstance(snapshot, dict), "observer snapshot is not an object")
        key = _generation_key(snapshot)
        require(key in unit_rows and snapshot.get("trigger_event_seq") in
                {event["seq"] for event in ([lifecycles[key]["new"]] +
                                           lifecycles[key]["properties"] +
                                           ([lifecycles[key]["removed"]]
                                            if lifecycles[key]["removed"] else []))},
                f"snapshot trigger is outside its lifecycle generation for {key[1]}")
        require(type(snapshot.get("started_monotonic_ns")) is int and
                type(snapshot.get("finished_monotonic_ns")) is int and
                ready_ns <= snapshot["started_monotonic_ns"] <= snapshot["finished_monotonic_ns"],
                f"snapshot time bounds are malformed for {key[1]}")
        require(type(snapshot.get("complete")) is bool and
                isinstance(snapshot.get("incomplete_reasons"), list) and
                all(isinstance(item, str) and item for item in snapshot["incomplete_reasons"]),
                f"snapshot completeness fields are malformed for {key[1]}")
        capture_state = snapshot.get("process_capture_state")
        require(capture_state is None or isinstance(capture_state, str),
                f"snapshot process capture state is malformed for {key[1]}")
        manager_properties_complete = snapshot.get("manager_properties_complete", False)
        process_capture_complete = snapshot.get("process_capture_complete", False)
        require(type(manager_properties_complete) is bool and type(process_capture_complete) is bool and
                (snapshot["complete"] == (manager_properties_complete and process_capture_complete)) and
                type(snapshot.get("preliminary_property_miss", False)) is bool,
                f"snapshot component completeness flags disagree for {key[1]}")
        if snapshot["complete"]:
            require(not snapshot["incomplete_reasons"],
                    f"snapshot claims complete with incomplete reasons for {key[1]}")
        else:
            require(bool(snapshot["incomplete_reasons"]) or
                    (not snapshot.get("process_started") and
                     snapshot.get("manager_properties_complete") is True and
                     snapshot.get("process_capture_complete") is False),
                    f"incomplete snapshot lacks a reason or expected PID-zero partial for {key[1]}")
        main_pid = snapshot.get("main_pid")
        started = snapshot.get("process_started")
        ticks = snapshot.get("start_ticks")
        require(type(main_pid) is int and 0 <= main_pid <= (1 << 31) - 1 and
                type(started) is bool and started == (main_pid > 0) and
                (ticks is None or (type(ticks) is int and ticks > 0 and started)),
                f"snapshot process identity is malformed for {key[1]}")
        if started and not snapshot["complete"]:
            require(capture_state in SUPERSEDABLE_CAPTURE_STATES and
                    snapshot.get("manager_properties_complete") is True and
                    snapshot.get("process_capture_complete") is False,
                    f"positive partial process capture is not explicitly supersedable for {key[1]}")
        initial_exe = snapshot.get("initial_exe_observed")
        require(initial_exe is None or
                (isinstance(initial_exe, str) and len(initial_exe) <= 4096 and
                 initial_exe.startswith("/") and "\x00" not in initial_exe),
                f"snapshot initial executable is malformed for {key[1]}")
        if capture_state == "known_preexec_executable_transition":
            known_failure = any(
                isinstance(reason, str) and reason.startswith("process_capture_failed:ProbeError:") and
                (re.fullmatch(r"process cgroup mismatch for broker-[0-9]{6}",
                              reason.split("process_capture_failed:ProbeError:", 1)[1]) is not None or
                 re.fullmatch(r"process executable path mismatch for broker-[0-9]{6}: " +
                              re.escape(initial_exe or ""),
                              reason.split("process_capture_failed:ProbeError:", 1)[1]) is not None)
                for reason in snapshot["incomplete_reasons"])
            require(started and type(ticks) is int and ticks > 0 and
                    initial_exe in PREEXEC_EXECUTABLES and
                    snapshot.get("active_state") == "activating" and
                    snapshot.get("substate") == "start" and
                    snapshot.get("manager_properties_complete") is True and
                    snapshot.get("process_capture_complete") is False and known_failure,
                    f"known pre-exec transition lacks its exact process/show witness for {key[1]}")
        invocation = snapshot.get("invocation_id")
        invocation_observed = snapshot.get("invocation_id_observed")
        require(invocation is None or
                (isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None and
                 invocation != "0" * 32),
                f"snapshot invocation ID is malformed for {key[1]}")
        require(invocation_observed is None or invocation_observed == "" or
                (isinstance(invocation_observed, str) and
                 HEX32.fullmatch(invocation_observed) is not None),
                f"snapshot observed invocation ID is malformed for {key[1]}")
        require(not snapshot["complete"] or
                (started and invocation is not None and type(ticks) is int and ticks > 0 and
                 type(snapshot.get("exec_main_start_timestamp_monotonic")) is int and
                 snapshot["exec_main_start_timestamp_monotonic"] > 0),
                f"complete snapshot lacks a full process identity for {key[1]}")
    if manager_reloading_seen:
        require(summary.get("event_loss_detected") is True,
                "manager Reloading signal was not treated as event loss")
    expected_manager_complete = (bool(unit_rows) and summary.get("stop_seen") is True and
        not global_reasons and summary.get("event_loss_detected") is False and
        all(row["manager_lifecycle_complete"] for row in unit_rows.values()))
    expected_process_complete = (bool(unit_rows) and summary.get("stop_seen") is True and
        not global_reasons and summary.get("event_loss_detected") is False and
        all(row["process_complete"] for row in unit_rows.values()))
    require(type(summary.get("manager_observation_complete")) is bool and
            summary["manager_observation_complete"] == expected_manager_complete and
            type(summary.get("process_complete")) is bool and
            summary["process_complete"] == expected_process_complete and
            type(summary.get("observer_complete")) is bool and
            summary["observer_complete"] == expected_process_complete,
            "observer summary completeness disagrees with its unit populations")
    return unit_rows, nested_snapshots


def parse_expected(raw: bytes) -> dict[str, Any]:
    value = strict_json(raw, "expected JSON")
    require(isinstance(value, dict) and value.get("schema") == SCHEMA,
            "expected JSON schema mismatch")
    required = {"run_id", "run_attempt", "job_id", "job_attempt", "boot_id", "boot_id_raw", "source_commit",
                "source_tree", "broker_binary_sha256", "broker_executable_path", "profile",
                "allowed_requests", "journal_capture"}
    require(set(value) == required | {"schema"},
            "expected JSON has unknown or missing top-level fields")
    require(isinstance(value["run_id"], str), "run_id must be a canonical decimal string")
    canonical_uint(value["run_id"], "run_id", (1 << 64) - 1, False)
    require(type(value["run_attempt"]) is int and 1 <= value["run_attempt"] <= (1 << 32) - 1,
            "invalid run_attempt")
    require(type(value["job_id"]) is int and 0 < value["job_id"] <= (1 << 64) - 1 and
            type(value["job_attempt"]) is int and 0 < value["job_attempt"] <= (1 << 64) - 1,
            "invalid exact broker job/attempt")
    require(isinstance(value["boot_id"], str) and HEX32.fullmatch(value["boot_id"]) is not None and
            boot_uuid(value["boot_id_raw"], "expected boot_id_raw") == value["boot_id"],
            "expected boot ID forms disagree")
    require(isinstance(value["source_commit"], str) and HEX40.fullmatch(value["source_commit"]) is not None and
            isinstance(value["source_tree"], str) and HEX40.fullmatch(value["source_tree"]) is not None,
            "source Git identities must be exact SHA-1 IDs")
    require(isinstance(value["broker_binary_sha256"], str) and
            HEX64.fullmatch(value["broker_binary_sha256"]) is not None,
            "installed broker binary SHA256 is malformed")
    require(isinstance(value["broker_executable_path"], str) and
            value["broker_executable_path"].startswith("/") and "\x00" not in value["broker_executable_path"] and
            ".." not in PurePosixPath(value["broker_executable_path"]).parts,
            "broker executable path is malformed")
    require(value["broker_executable_path"] == "/usr/local/libexec/buster-bench-systemd-broker",
            "broker executable path differs from the installed service policy")
    require(isinstance(value["allowed_requests"], list) and 1 <= len(value["allowed_requests"]) <= 32,
            "allowed request tuple list is missing or oversized")
    seen_requests: set[tuple[int, int, int, int]] = set()
    for row in value["allowed_requests"]:
        require(isinstance(row, dict) and set(row) == {"operation", "stage", "job", "attempt"},
                "allowed request tuple has unknown or missing fields")
        op = row["operation"]
        stage = row["stage"]
        job = row["job"]
        attempt = row["attempt"]
        require(type(op) is int and op in (1, 2) and type(stage) is int and 0 <= stage <= 5 and
                type(job) is int and 0 < job <= (1 << 64) - 1 and
                type(attempt) is int and 0 < attempt <= (1 << 64) - 1,
                "invalid allowed request tuple")
        key = (op, stage, job, attempt)
        require(key not in seen_requests, "duplicate allowed request tuple")
        seen_requests.add(key)
    require(any(row["job"] == value["job_id"] and row["attempt"] == value["job_attempt"]
                for row in value["allowed_requests"]),
            "allowed request list omits exact run job/attempt")
    profile = value["profile"]
    require(isinstance(profile, dict), "profile must be an object")
    expected_profile_keys = {"broker_uid", "broker_gid", "candidate_gid", "client_uids",
                             "groups", "capabilities", "no_new_privs", "seccomp",
                             "seccomp_filters_min", "read_only_mount_targets", "cgroup_prefix"}
    require(set(profile) == expected_profile_keys, "profile has unknown or missing fields")
    for key in ("broker_uid", "broker_gid", "candidate_gid", "no_new_privs", "seccomp",
                "seccomp_filters_min"):
        require(type(profile[key]) is int and 0 <= profile[key] <= (1 << 32) - 1,
                f"invalid profile {key}")
    require(isinstance(profile["client_uids"], list) and profile["client_uids"] and
            all(type(item) is int and 0 <= item <= (1 << 32) - 1 for item in profile["client_uids"]) and
            len(profile["client_uids"]) == len(set(profile["client_uids"])),
            "profile client_uids are invalid")
    require(isinstance(profile["groups"], list) and profile["groups"] and
            all(type(item) is int and 0 <= item <= (1 << 32) - 1 for item in profile["groups"]) and
            profile["groups"] == sorted(set(profile["groups"])), "profile groups are invalid")
    require(profile["broker_uid"] == 0 and profile["broker_gid"] == 65000 and
            profile["candidate_gid"] == 65001 and profile["groups"] == [65000, 65001] and
            profile["client_uids"] == [65000] and
            profile["no_new_privs"] == 1 and profile["seccomp"] == 2 and
            profile["seccomp_filters_min"] >= 1,
            "expected profile differs from the fixed broker identity policy")
    caps = profile["capabilities"]
    require(isinstance(caps, dict) and set(caps) == {"CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb"} and
            all(isinstance(item, str) and re.fullmatch(r"[0-9a-f]{16}", item) is not None for item in caps.values()),
            "profile capability expectations are invalid")
    require(all(item == "0000000000000000" for item in caps.values()),
            "broker profile must explicitly expect zero capabilities")
    targets = profile["read_only_mount_targets"]
    require(isinstance(targets, list) and targets and len(targets) <= 32 and
            all(isinstance(item, str) and item.startswith("/") and
                ".." not in PurePosixPath(item).parts for item in targets) and
            targets == ["/", "/etc/buster-bench", "/opt/buster-bench/installed",
                        "/var/lib/buster-bench"] and
            len(targets) == len(set(targets)), "read-only mount target profile is invalid")
    require(isinstance(profile["cgroup_prefix"], str) and profile["cgroup_prefix"].startswith("/") and
            profile["cgroup_prefix"].endswith("/") and
            ".." not in profile["cgroup_prefix"].split("/") and
            profile["cgroup_prefix"] ==
            "/system.slice/system-buster\\x2dbench\\x2dsystemd\\x2dbroker.slice/",
            "cgroup prefix is invalid")
    capture = value["journal_capture"]
    require(isinstance(capture, dict) and set(capture) == {"complete", "sha256", "bytes", "records"} and
            capture["complete"] is True and isinstance(capture["sha256"], str) and
            HEX64.fullmatch(capture["sha256"]) is not None and type(capture["bytes"]) is int and
            0 < capture["bytes"] <= MAX_FILE and type(capture["records"]) is int and
            1 <= capture["records"] <= 1_000_000,
            "journal capture completeness descriptor is invalid")
    return value


def _validate_journal_capture(raw: bytes, expected: dict[str, Any]) -> list[dict[str, Any]]:
    capture = expected["journal_capture"]
    require(len(raw) == capture["bytes"] and hashlib.sha256(raw).hexdigest() == capture["sha256"],
            "journal capture byte count or SHA256 differs from expectation")
    records = parse_jsonl(raw, "journal JSONL")
    require(len(records) == capture["records"],
            "journal capture record count differs from expectation")
    # Inspect every identifying field before the manager/diagnostic collectors
    # filter records. Absence cannot be established from a binary/array value.
    for record in records:
        for field in JOURNAL_UNIT_FIELDS:
            if field in record:
                require(isinstance(record[field], str),
                        f"journal {field} is not a scalar unit name")
                require(record[field] != TEMPLATE_METADATA_UNIT,
                        f"journal {field} identifies internal template activity")
    return records


def _validate_diagnostic_profiles(
        diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
        expected: dict[str, Any]) -> dict[tuple[str, str, str, int], dict[str, Any]]:
    verified: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    for key, entry in diagnostics.items():
        sequence = entry["sequence"]
        require(sequence["exe"]["path"] == expected["broker_executable_path"],
                f"C diagnostic executable path differs for {key[1]}")
        _status_identity(sequence["status"], sequence["pid"], expected["profile"],
                         f"{key[1]} C diagnostic")
        cgroup_path = _parse_cgroup(sequence["cgroup"], expected["profile"]["cgroup_prefix"],
                                    f"{key[1]} C diagnostic")
        ro_mounts = _readonly_mounts(sequence["mountinfo"],
                                     expected["profile"]["read_only_mount_targets"],
                                     f"{key[1]} C diagnostic")
        require(all(ro_mounts.values()), f"C diagnostic reports a writable required mount for {key[1]}")
        verified[key] = {"cgroup_path": cgroup_path, "ro_mounts": ro_mounts,
                         "status": sequence["status"], "socket": sequence["socket"]}
    return verified


def _observer_unit_index(
        unit_rows: dict[tuple[str, str, str, int], dict[str, Any]]) -> dict[tuple[str, str], tuple[str, str, str, int]]:
    result: dict[tuple[str, str], tuple[str, str, str, int]] = {}
    for key, row in unit_rows.items():
        pair = (key[0], key[1])
        require(pair not in result,
                f"observer has multiple lifecycle generations for one manager unit: {key[1]}")
        result[pair] = key
    return result


def _validate_retained_terminal_join(
        row: dict[str, Any], manager: dict[str, Any], diagnostic: dict[str, Any],
        failed_rows: dict[tuple[str, str, str], dict[str, Any]],
        expected: dict[str, Any], observer_fd: int) -> None:
    terminal = row["retained_failed_terminal"]
    unit = row["unit"]
    declared = terminal["properties"]
    raw = _safe_ref(observer_fd, terminal["raw_show"], "retained failed all-show", 256 * 1024)
    stderr = _safe_ref(observer_fd, terminal["raw_stderr"], "retained failed stderr", 32 * 1024)
    require(not stderr and terminal["raw_show"]["exit"] == terminal["raw_stderr"]["exit"] == 0 and
            terminal["raw_show"]["timed_out"] is False and terminal["raw_stderr"]["timed_out"] is False,
            "retained failed raw query did not complete cleanly")
    observation = {"process_started": False, "main_pid": 0, "result": "exit-code",
                   "substate": "failed", "exec_main_status": "1", "active_state": "failed",
                   "exec_main_start_timestamp_monotonic": int(declared["ExecMainStartTimestampMonotonic"])}
    properties = _parse_systemctl_show(raw, unit, row["invocation_id"], observation, expected)
    require(TERMINAL_PROPERTIES <= properties.keys() and
            {key: properties[key] for key in TERMINAL_PROPERTIES} == declared,
            "retained failed summary differs from complete raw manager readback")
    failure_key = (row["boot_id"], unit, row["invocation_id"])
    require(failure_key in failed_rows, "retained failed unit lacks trusted PID1 exit/failure pair")
    failure = failed_rows[failure_key]
    exit_row, result_row = failure["exit"], failure["failure"]
    start_us = int(properties["ExecMainStartTimestampMonotonic"])
    exit_us = int(properties["ExecMainExitTimestampMonotonic"])
    require(manager["result"] == "done" and
            manager["record_number"] == failure["start_record_number"] and
            start_us <= failure["start_monotonic_us"] <= exit_row["monotonic_us"] and
            start_us < exit_us <= exit_row["monotonic_us"] <= result_row["monotonic_us"] and
            result_row["monotonic_us"] * 1000 <= terminal["started_monotonic_ns"] and
            exit_row["code"] == "exited" and exit_row["status"] == properties["ExecMainStatus"] == "1" and
            result_row["result"] == properties["Result"] == "exit-code",
            "retained failed raw result/timing disagrees with trusted PID1 outcome")
    sequence = diagnostic["sequence"]
    outcome = sequence["outcome"]
    require(sequence["pid"] == int(properties["ExecMainPID"]) == row["main_pid"] and
            sequence["start_ticks"] == row["start_ticks"] and
            outcome["broker_exit"] == 1 and outcome["frame_status"] == 126 and outcome["frame_sent"] == 1,
            "retained failure differs from the same process's complete broker diagnostic")


def _validate_manager_population(
        manager_rows: dict[tuple[str, str], dict[str, Any]],
        unit_rows: dict[tuple[str, str, str, int], dict[str, Any]],
        diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
        summary: dict[str, Any],
        failed_rows: dict[tuple[str, str, str], dict[str, Any]] | None = None,
        expected: dict[str, Any] | None = None, observer_fd: int | None = None) -> dict[str, int]:
    observer_index = _observer_unit_index(unit_rows)
    require(bool(manager_rows) and set(manager_rows) == set(observer_index),
            "trusted PID 1 starts and observed manager lifecycles have different populations")
    require(summary.get("manager_observation_complete") is True,
            "observer manager lifecycle is incomplete")
    diagnostic_index: dict[tuple[str, str], tuple[str, str, str, int]] = {}
    for diagnostic_key in diagnostics:
        pair = (diagnostic_key[0], diagnostic_key[1])
        require(pair not in diagnostic_index,
                f"multiple diagnostic streams map to one broker unit: {diagnostic_key[1]}")
        require(pair in manager_rows, f"diagnostic has no trusted PID 1 start result: {diagnostic_key[1]}")
        diagnostic_index[pair] = diagnostic_key
    started_count = 0
    for pair, manager in manager_rows.items():
        observer_key = observer_index[pair]
        unit_row = unit_rows[observer_key]
        require(unit_row["manager_lifecycle_complete"] is True,
                f"observer manager lifecycle is incomplete for {pair[1]}")
        manager_invocation = manager.get("invocation_id")
        unit_invocation = unit_row.get("invocation_id")
        require(manager_invocation is None or unit_invocation is None or
                manager_invocation == unit_invocation,
                f"PID 1 invocation differs from observer lifecycle for {pair[1]}")
        diagnostic_key = diagnostic_index.get(pair)
        if unit_row["process_start_seen"]:
            require(unit_row["process_started"] and type(unit_row["main_pid"]) is int and
                    unit_row["main_pid"] > 0 and type(unit_row["start_ticks"]) is int and
                    unit_row["start_ticks"] > 0 and isinstance(unit_invocation, str) and
                    HEX32.fullmatch(unit_invocation) is not None and diagnostic_key is not None,
                    f"started broker lifecycle lacks a complete diagnostic identity for {pair[1]}")
            entry = diagnostics[diagnostic_key]
            require(diagnostic_key[2] == unit_invocation and
                    diagnostic_key[3] == unit_row["main_pid"] and
                    entry["sequence"]["start_ticks"] == unit_row["start_ticks"] and
                    (manager_invocation is None or manager_invocation == unit_invocation),
                    f"PID 1, observer and diagnostic process identities differ for {pair[1]}")
            if unit_row.get("retained_failed_terminal") is not None:
                require(failed_rows is not None and expected is not None and observer_fd is not None,
                        "retained terminal lacks raw/journal verification inputs")
                _validate_retained_terminal_join(unit_row, manager, entry, failed_rows, expected, observer_fd)
            started_count += 1
        else:
            require(not unit_row["process_started"] and diagnostic_key is None,
                    f"broker process diagnostic has no observed start event for {pair[1]}")
    require(started_count > 0, "no started broker process was joined across evidence sources")
    return {"manager_units": len(manager_rows), "observer_generations": len(unit_rows),
            "joined_processes": started_count}


def _check_complete_capture_conservation(
        full_captures: list[dict[str, Any]], diagnostic_profile: dict[str, Any],
        start_ticks: int, unit: str) -> None:
    require(bool(full_captures), f"no complete process capture for {unit}")
    # Every retained complete capture must agree. Selecting only the first
    # witness would hide a later contradictory socket or security fact.
    for full in full_captures:
        require(full["start_ticks"] == start_ticks and
                full["socket"] is not None and
                full["socket"]["inode"] == diagnostic_profile["socket"]["ino"] and
                full["socket"]["device"] == diagnostic_profile["socket"]["dev"] and
                full["socket"]["mode"] == diagnostic_profile["socket"]["mode"] and
                full["proc"] is not None and
                _same_security_status(full["proc"]["status"], diagnostic_profile["status"]) and
                full["proc"]["cgroup_path"] == diagnostic_profile["cgroup_path"] and
                full["proc"]["ro_mounts"] == diagnostic_profile["ro_mounts"],
                f"C diagnostic and external process captures disagree for {unit}")


def _validate_external_population(
        unit_rows: dict[tuple[str, str, str, int], dict[str, Any]],
        snapshots: list[dict[str, Any]],
        diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
        diagnostic_profiles: dict[tuple[str, str, str, int], dict[str, Any]],
        expected: dict[str, Any], observer_fd: int,
        summary: dict[str, Any]) -> tuple[dict[tuple[str, str, str, int], dict[str, Any]], dict[str, int]]:
    require(summary.get("process_complete") is True and summary.get("observer_complete") is True,
            "observer process population is incomplete")
    verified_snapshots: dict[tuple[str, str, str, int], list[dict[str, Any]]] = {
        key: [] for key in unit_rows}
    for snapshot in snapshots:
        key = _generation_key(snapshot)
        require(key in unit_rows, "snapshot does not map to an observed lifecycle generation")
        verified_snapshots[key].append(
            _validate_snapshot_capture(snapshot, unit_rows[key], expected, observer_fd))
    full_witnesses: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    for key, unit_row in unit_rows.items():
        require(unit_row["process_complete"] is True and
                unit_row["manager_lifecycle_complete"] is True and
                unit_row["process_started"] is True,
                f"observer has no complete broker process witness for {key[1]}")
        positive = [snapshot for snapshot in unit_row["snapshots"]
                    if snapshot["main_pid"] > 0]
        complete_positive = [snapshot for snapshot in positive if snapshot["complete"]]
        require(bool(complete_positive), f"broker generation lacks a full process capture: {key[1]}")
        identities = {_snapshot_process_identity(snapshot) for snapshot in positive}
        require(None not in identities and len(identities) == 1,
                f"broker generation has a missing or conflicting process identity: {key[1]}")
        identity = next(iter(identities))
        require(all(_snapshot_process_identity(snapshot) == identity for snapshot in complete_positive),
                f"broker full snapshots disagree on process identity for {key[1]}")
        require(unit_row["invocation_id"] == identity[4] and unit_row["main_pid"] == identity[5] and
                unit_row["cgroup"] == identity[7] and unit_row["start_ticks"] == identity[8] and
                unit_row["exe"] == expected["broker_executable_path"] and
                unit_row["exe_sha256"] == expected["broker_binary_sha256"],
                f"observer summary identity differs from full process witness for {key[1]}")
        process_verifications = verified_snapshots[key]
        require(len(process_verifications) == len(unit_row["snapshots"]) and
                any(item["complete"] and item["proc"] is not None and item["socket"] is not None
                    for item in process_verifications),
                f"broker generation has no verified manager/proc/fd0 witness for {key[1]}")
        for original, checked in zip(unit_row["snapshots"], process_verifications):
            if original["main_pid"] > 0:
                require(checked["process_identity"] == identity,
                        f"partial process capture changed identity within {key[1]}")
                for field, expected_index in (("invocation_id", 4), ("main_pid", 5),
                                              ("exec_main_start_timestamp_monotonic", 6),
                                              ("cgroup", 7), ("start_ticks", 8)):
                    observed = original.get(field)
                    if observed is not None:
                        require(observed == identity[expected_index],
                                f"partial process capture {field} conflicts within {key[1]}")
            elif original.get("invocation_id") is not None:
                require(original["invocation_id"] == identity[4],
                        f"PID-zero snapshot changed invocation identity for {key[1]}")
        diagnostic_key = (key[0], key[1], identity[4], identity[5])
        require(diagnostic_key in diagnostics and diagnostic_key in diagnostic_profiles,
                f"full process witness has no exact trusted stdout diagnostic for {key[1]}")
        diagnostic_profile = diagnostic_profiles[diagnostic_key]
        full_captures = [item for item in process_verifications if item["complete"]]
        require(diagnostics[diagnostic_key]["sequence"]["start_ticks"] == identity[8],
                f"C diagnostic start ticks differ from observer for {key[1]}")
        _check_complete_capture_conservation(
            full_captures, diagnostic_profile, identity[8], key[1])
        full = full_captures[0]
        full_witnesses[key] = {"process_identity": identity, "socket": full["socket"],
                               "status": full["proc"]["status"], "proc": full["proc"]}
    return full_witnesses, {"process_generations": len(full_witnesses),
                            "full_snapshots": sum(1 for rows in verified_snapshots.values()
                                                  for item in rows if item["complete"]),
                            "partial_snapshots": sum(1 for rows in verified_snapshots.values()
                                                     for item in rows if not item["complete"])}


def _overlap_ref(root_fd: int, ref: Any, label: str, maximum: int) -> bytes:
    require(isinstance(ref, dict) and set(ref) == {"path", "sha256", "bytes"} and
            isinstance(ref.get("path"), str) and isinstance(ref.get("sha256"), str) and
            HEX64.fullmatch(ref["sha256"]) is not None and type(ref.get("bytes")) is int and
            0 <= ref["bytes"] <= maximum,
            f"{label} overlap reference is malformed")
    raw = read_regular_at(root_fd, ref["path"], maximum, allow_empty=True)
    require(len(raw) == ref["bytes"] and hashlib.sha256(raw).hexdigest() == ref["sha256"],
            f"{label} overlap reference byte count or SHA256 mismatch")
    return raw


def _validate_overlap_rows(raw: bytes, overlap_parent_fd: int,
                           expected: dict[str, Any], unit_rows: dict[tuple[str, str, str, int], dict[str, Any]],
                           full_witnesses: dict[tuple[str, str, str, int], dict[str, Any]],
                           diagnostics: dict[tuple[str, str, str, int], dict[str, Any]],
                           diagnostic_profiles: dict[tuple[str, str, str, int], dict[str, Any]]) -> dict[str, int]:
    rows = parse_jsonl(raw, "overlap JSONL") if raw else []
    require(len(rows) <= MAX_INSTANCES, "overlap row count exceeds bound")
    seen: set[tuple[str, str, str, int, int]] = set()
    for number, row in enumerate(rows, 1):
        fields = {"schema", "boot_id", "unit", "invocation_id", "pid", "start_ticks",
                  "capture_json", "status", "mountinfo", "cgroup"}
        require(set(row) == fields and row.get("schema") == "issue1162-broker-overlap-v1",
                f"overlap row {number} schema has unknown or missing fields")
        boot = row.get("boot_id")
        unit = row.get("unit")
        invocation = row.get("invocation_id")
        pid = row.get("pid")
        ticks = row.get("start_ticks")
        require(boot == expected["boot_id"] and isinstance(unit, str),
                f"overlap row {number} boot or unit differs")
        parse_broker_unit(unit, f"overlap row {number} unit")
        require(isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None and
                invocation != "0" * 32 and type(pid) is int and 0 < pid <= (1 << 31) - 1 and
                type(ticks) is int and 0 < ticks <= (1 << 64) - 1,
                f"overlap row {number} process identity is malformed")
        key = (boot, unit, invocation, pid, ticks)
        require(key not in seen, f"duplicate overlap process identity in row {number}")
        seen.add(key)
        matching_generation = [generation for generation, witness in full_witnesses.items()
                              if generation[0] == boot and generation[1] == unit and
                              witness["process_identity"][4] == invocation and
                              witness["process_identity"][5] == pid and
                              witness["process_identity"][8] == ticks]
        require(len(matching_generation) == 1,
                f"overlap row {number} has no exact independent process witness")
        generation = matching_generation[0]
        diagnostic_key = (boot, unit, invocation, pid)
        require(generation in unit_rows and diagnostic_key in diagnostics and
                diagnostic_key in diagnostic_profiles,
                f"overlap row {number} has no exact broker diagnostic/process join")
        capture_raw = _overlap_ref(overlap_parent_fd, row["capture_json"],
                                   f"row {number} capture JSON", 4 * 1024 * 1024)
        status_raw = _overlap_ref(overlap_parent_fd, row["status"], f"row {number} status", 1024 * 1024)
        mount_raw = _overlap_ref(overlap_parent_fd, row["mountinfo"], f"row {number} mountinfo",
                                 16 * 1024 * 1024)
        cgroup_raw = _overlap_ref(overlap_parent_fd, row["cgroup"], f"row {number} cgroup", 1024 * 1024)
        capture = strict_json(capture_raw, f"overlap row {number} raw capture JSON")
        require(isinstance(capture, dict) and isinstance(capture.get("process"), dict) and
                isinstance(capture.get("properties"), dict),
                f"overlap row {number} capture JSON lacks process/properties")
        process = capture["process"]
        properties = capture["properties"]
        witness = full_witnesses[generation]
        identity = witness["process_identity"]
        require(process.get("pid") == pid and process.get("starttime_ticks") == ticks and
                process.get("exe") == expected["broker_executable_path"] and
                process.get("exe_sha256") == expected["broker_binary_sha256"] and
                process.get("expected_exe") == expected["broker_executable_path"] and
                process.get("exe_hash_matches_installed") is True and
                process.get("cgroup_matches_unit") is True and
                process.get("status_bytes") == len(status_raw) and
                process.get("mountinfo_bytes") == len(mount_raw),
                f"overlap row {number} process capture identity or size differs")
        require(properties.get("Id") == unit and properties.get("InvocationID") == invocation and
                properties.get("MainPID") == str(pid) and properties.get("ExecMainPID") == str(pid) and
                properties.get("ExecMainStartTimestampMonotonic") == str(identity[6]) and
                properties.get("ControlGroup") == identity[7] and
                properties.get("ProtectSystem") == "strict" and
                properties.get("ReadOnlyPaths", "").split() ==
                expected["profile"]["read_only_mount_targets"][1:],
                f"overlap row {number} systemd properties differ from observer")
        cgroup_path = _parse_cgroup(cgroup_raw, expected["profile"]["cgroup_prefix"],
                                    f"overlap row {number}")
        require(process.get("cgroup") == f"0::{cgroup_path}" and cgroup_path == identity[7],
                f"overlap row {number} proc cgroup differs from observer")
        status = _check_profile_status(status_raw, pid, expected["profile"], f"overlap row {number}")
        require(_same_security_status(status, witness["status"]),
                f"overlap row {number} security status differs from independent process capture")
        ro_mounts = _readonly_mounts(mount_raw, expected["profile"]["read_only_mount_targets"],
                                     f"overlap row {number}")
        require(ro_mounts == witness["proc"]["ro_mounts"] and
                ro_mounts == diagnostic_profiles[diagnostic_key]["ro_mounts"],
                f"overlap row {number} read-only mount facts differ from independent captures")
    return {"rows": len(rows), "matched_generations": len(seen),
            "observed_generations": len(unit_rows)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--journal-jsonl")
    parser.add_argument("--observer-dir")
    parser.add_argument("--expected-json")
    parser.add_argument("--overlap-jsonl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.journal_jsonl or not args.observer_dir or not args.expected_json:
        parser.error("--journal-jsonl, --observer-dir, and --expected-json are required")
    errors: list[dict[str, str]] = []
    result: dict[str, Any] = {
        "schema": "issue1162-broker-evidence-result-v1",
        "diagnostic_complete": False,
        "manager_capture_complete": False,
        "external_proc_complete": False,
        "overlap_complete": False,
        "full_ready": False,
        "journal_capture_complete": False,
        "observer_capture_complete": False,
        "overlap_input_supplied": args.overlap_jsonl is not None,
        "overlap_row_count": None,
        "overlap_scope": ("not_supplied" if args.overlap_jsonl is None else "unknown"),
        "diagnostic_counts": None,
        "manager_counts": None,
        "external_counts": None,
        "overlap_counts": None,
        "errors": errors,
    }

    def failed(component: str, exc: BaseException) -> None:
        message = str(exc).replace("\x00", "?")[:512] or type(exc).__name__
        errors.append({"component": component, "message": message})

    expected: dict[str, Any] | None = None
    try:
        expected = parse_expected(read_absolute_file(args.expected_json, MAX_FILE))
    except (EvidenceError, OSError, ValueError) as exc:
        failed("expected", exc)

    records: list[dict[str, Any]] | None = None
    diagnostics: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    diagnostic_profiles: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    manager_rows: dict[tuple[str, str], dict[str, Any]] = {}
    failed_rows: dict[tuple[str, str, str], dict[str, Any]] = {}
    if expected is not None:
        try:
            journal_raw = read_absolute_file(args.journal_jsonl, MAX_FILE)
            records = _validate_journal_capture(journal_raw, expected)
            result["journal_capture_complete"] = True
        except (EvidenceError, OSError, ValueError) as exc:
            failed("journal", exc)
        if records is not None:
            try:
                diagnostics = collect_diagnostics(records, expected["boot_id"])
                diag_counts = validate_diagnostic_request_set(diagnostics, expected)
                diagnostic_profiles = _validate_diagnostic_profiles(diagnostics, expected)
                result["diagnostic_counts"] = diag_counts
                result["diagnostic_complete"] = True
            except (EvidenceError, OSError, ValueError) as exc:
                failed("diagnostic", exc)
            try:
                manager_rows = collect_manager_start_rows(records, expected["boot_id"])
                failed_rows = collect_manager_failed_exit_rows(records, expected["boot_id"])
            except (EvidenceError, OSError, ValueError) as exc:
                manager_rows = {}
                failed("manager_journal", exc)

    observer_fd: int | None = None
    unit_rows: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    snapshots: list[dict[str, Any]] = []
    summary: dict[str, Any] | None = None
    if expected is not None:
        try:
            observer_fd = open_absolute_directory(args.observer_dir)
            ready, events, summary = _observer_documents(observer_fd, expected)
            if summary.get("template_metadata_events"):
                require(result["journal_capture_complete"] and records is not None,
                        "template metadata lacks a complete original broker journal")
            unit_rows, snapshots = _validate_event_rows(ready, events, summary)
            result["observer_capture_complete"] = True
        except (EvidenceError, OSError, ValueError) as exc:
            failed("observer", exc)

    if expected is not None and summary is not None and result["journal_capture_complete"]:
        try:
            result["manager_counts"] = _validate_manager_population(
                manager_rows, unit_rows, diagnostics, summary, failed_rows, expected, observer_fd)
            result["manager_capture_complete"] = True
        except (EvidenceError, OSError, ValueError) as exc:
            failed("manager_capture", exc)

    full_witnesses: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    if expected is not None and observer_fd is not None and summary is not None:
        try:
            require(bool(diagnostic_profiles), "diagnostic profile evidence is unavailable")
            full_witnesses, external_counts = _validate_external_population(
                unit_rows, snapshots, diagnostics, diagnostic_profiles, expected, observer_fd, summary)
            result["external_counts"] = external_counts
            result["external_proc_complete"] = True
        except (EvidenceError, OSError, ValueError) as exc:
            failed("external_proc", exc)

    overlap_rows_for_gate: int | None = None
    overlap_is_consistent_for_gate = args.overlap_jsonl is None
    overlap_parent_fd: int | None = None
    if args.overlap_jsonl is not None:
        try:
            require(isinstance(args.overlap_jsonl, str) and args.overlap_jsonl.startswith("/"),
                    "overlap JSONL path must be absolute")
            overlap_path = PurePosixPath(args.overlap_jsonl)
            overlap_parent_fd = open_absolute_directory(str(overlap_path.parent))
            overlap_raw = read_regular_at(overlap_parent_fd, overlap_path.name,
                                          MAX_OVERLAP, allow_empty=True)
            overlap_rows = parse_jsonl(overlap_raw, "overlap JSONL") if overlap_raw else []
            overlap_rows_for_gate = len(overlap_rows)
            result["overlap_row_count"] = overlap_rows_for_gate
            result["overlap_scope"] = ("sparse_legacy_rows" if overlap_rows else "sparse_no_rows")
            if overlap_rows:
                require(expected is not None and observer_fd is not None and summary is not None and
                        result["external_proc_complete"],
                        "nonempty overlap rows lack independently complete process witnesses")
                require(bool(diagnostic_profiles), "overlap rows lack parsed C diagnostic profile evidence")
                overlap_counts = _validate_overlap_rows(overlap_raw, overlap_parent_fd, expected,
                    unit_rows, full_witnesses, diagnostics, diagnostic_profiles)
                result["overlap_counts"] = overlap_counts
                result["overlap_complete"] = True
                overlap_is_consistent_for_gate = True
            else:
                result["overlap_counts"] = {"rows": 0, "matched_generations": 0,
                                             "observed_generations": len(unit_rows)}
                overlap_is_consistent_for_gate = True
        except (EvidenceError, OSError, ValueError) as exc:
            failed("overlap", exc)
            result["overlap_scope"] = "invalid_or_unreadable"
            overlap_is_consistent_for_gate = False

    if overlap_parent_fd is not None:
        os.close(overlap_parent_fd)
    if observer_fd is not None:
        os.close(observer_fd)

    # The helper reports independent evidence components only. Human/source
    # acceptance is intentionally outside this offline consumer.
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    required_complete = (result["diagnostic_complete"] and
                        result["manager_capture_complete"] and
                        result["external_proc_complete"])
    return 0 if required_complete and overlap_is_consistent_for_gate else 2


def self_test() -> int:
    checks = 0

    def rejects(function: Any, *args: Any) -> None:
        nonlocal checks
        try:
            function(*args)
        except (EvidenceError, OSError, ValueError):
            checks += 1
        else:
            raise AssertionError(f"negative fixture unexpectedly passed: {function.__name__}")

    # This embedded stream is a frozen C-producer grammar capture with
    # synthetic fields and no trusted journal metadata. Normalize its sample
    # PID only to make the internal /proc stat consistency check meaningful.
    producer_messages = zlib.decompress(base64.b64decode(_PRODUCER_FIXTURE_ZLIB_B64)).decode(
        "ascii", "strict").splitlines()
    producer_messages = [line.replace("pid=10", "pid=459306") for line in producer_messages]
    producer_messages = [line.replace("peer_uid=0", "peer_uid=65000") for line in producer_messages]
    sequence = parse_sequence(producer_messages)
    require(sequence["pid"] == 459306 and sequence["request"]["peer_uid"] == 65000,
            "synthetic producer grammar fixture did not parse expected IDs")
    checks += 1
    rejects(parse_sequence, producer_messages + [producer_messages[-1]])
    rejects(_parse_message, "BQ-BROKER-DIAG-V1 OUTCOME pid=1 ticks=2\r")

    root_ro = (b"1 1 0:1 / / ro - rootfs rootfs ro\n"
               b"2 1 0:2 / /sys/fs/cgroup/system.slice/system-buster\\134x2dbench\\134x2dsystemd\\134x2dbroker.slice/memory.pressure rw - cgroup2 cgroup rw\n")
    targets = ["/", "/etc/buster-bench", "/opt/buster-bench/installed", "/var/lib/buster-bench"]
    mounts = _readonly_mounts(root_ro, targets, "self-test root-cover")
    require(all(mounts.values()), "read-only root cover fixture failed")
    checks += 1
    nested_rw = root_ro + b"3 1 0:3 / /etc/buster-bench rw - ext4 /dev/test rw\n"
    rejects(_readonly_mounts, nested_rw, targets, "self-test nested writable")
    sibling_prefix = (b"1 1 0:1 / / rw - rootfs rootfs rw\n"
                      b"2 1 0:2 / /etc/buster-bench2 ro - ext4 /dev/test ro\n")
    rejects(_readonly_mounts, sibling_prefix, ["/etc/buster-bench"], "self-test sibling prefix")
    rejects(_readonly_mounts, b"1 1 0:1 / / ro - rootfs rootfs ro\n"
            b"2 1 0:2 / /etc\\999bad rw - ext4 /dev/test rw\n",
            ["/etc/buster-bench"], "self-test malformed escape")
    duplicate_root = b"1 1 0:1 / / ro - rootfs rootfs ro\n2 1 0:2 / / rw - rootfs rootfs rw\n"
    rejects(_readonly_mounts, duplicate_root, ["/"], "self-test duplicate mount")

    boot_raw = "01234567-89ab-cdef-0123-456789abcdef"
    expected = {
        "schema": SCHEMA, "run_id": "123", "run_attempt": 1, "job_id": 11,
        "job_attempt": 1, "boot_id": boot_raw.replace("-", ""), "boot_id_raw": boot_raw,
        "source_commit": "a" * 40, "source_tree": "b" * 40,
        "broker_binary_sha256": "c" * 64,
        "broker_executable_path": "/usr/local/libexec/buster-bench-systemd-broker",
        "profile": {"broker_uid": 0, "broker_gid": 65000, "candidate_gid": 65001,
            "client_uids": [65000], "groups": [65000, 65001],
            "capabilities": {key: "0000000000000000" for key in
                              ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb")},
            "no_new_privs": 1, "seccomp": 2, "seccomp_filters_min": 1,
            "read_only_mount_targets": targets,
            "cgroup_prefix": "/system.slice/system-buster\\x2dbench\\x2dsystemd\\x2dbroker.slice/"},
        "allowed_requests": [{"operation": 1, "stage": 4, "job": 11, "attempt": 1}],
        "journal_capture": {"complete": True, "sha256": "d" * 64, "bytes": 1, "records": 1},
    }
    require(parse_expected(json.dumps(expected).encode())["profile"]["client_uids"] == [65000],
            "fixed expected profile fixture failed")
    checks += 1
    expected_bad_uid = json.loads(json.dumps(expected))
    expected_bad_uid["profile"]["client_uids"] = [0, 65000]
    rejects(parse_expected, json.dumps(expected_bad_uid).encode())

    # The metadata pair cannot hide execution, dropped events, or unbounded
    # observations. These fixtures exercise the production shape and raw
    # journal adapters; full CLI producer/consumer fixtures cover their join.
    metadata_ready = {"boot_id": expected["boot_id"], "boot_id_raw": boot_raw,
                      "manager_sender": ":1.0", "ready_monotonic_ns": 100}
    metadata_events = [{"event": "UnitNew", "monotonic_ns": 400,
                        "unit": "buster-bench-systemd-broker@0-789-65000.service"}]
    pair = [{"boot_id": expected["boot_id"], "boot_id_raw": boot_raw,
             "unit": TEMPLATE_METADATA_UNIT, "object_path": TEMPLATE_METADATA_PATH,
             "sender": ":1.0", "member": member,
             "interface": "org.freedesktop.systemd1.Manager",
             "path": "/org/freedesktop/systemd1", "signature": "so",
             "bus_dispatch_ordinal": ordinal, "monotonic_ns": ns}
            for member, ordinal, ns in (("UnitNew", 1, 200), ("UnitRemoved", 2, 300))]
    metadata_summary = {"template_metadata_events": pair, "bus_dispatch_count": 3,
                        "ended_monotonic_ns": 500, "stop_seen": True,
                        "event_stream_complete": True, "event_loss_detected": False,
                        "global_incomplete_reasons": []}
    _validate_template_metadata(metadata_ready, metadata_events, metadata_summary)
    _validate_template_metadata(metadata_ready, metadata_events, {})
    _validate_template_metadata(metadata_ready, metadata_events, {"template_metadata_events": []})
    checks += 3
    for invalid_pair in (None, {}, pair[:1], pair + pair, list(reversed(pair))):
        rejects(_validate_template_metadata, metadata_ready, metadata_events,
                {**metadata_summary, "template_metadata_events": invalid_pair})
    for key, value in (("boot_id", "b" * 32), ("boot_id_raw", "bad-uuid"),
                       ("unit", TEMPLATE_METADATA_UNIT + "x"), ("object_path", "/unrelated"),
                       ("sender", ":1.2"), ("member", "PropertiesChanged"),
                       ("interface", "org.freedesktop.DBus.Properties"), ("path", "/wrong"),
                       ("signature", "s"), ("event", "UnitNew"),
                       ("bus_dispatch_ordinal", True), ("bus_dispatch_ordinal", 0),
                       ("bus_dispatch_ordinal", 3), ("bus_dispatch_ordinal", 4097),
                       ("monotonic_ns", True), ("monotonic_ns", 0),
                       ("monotonic_ns", 99), ("monotonic_ns", 300),
                       ("monotonic_ns", 400), ("monotonic_ns", 501)):
        changed = json.loads(json.dumps(metadata_summary))
        changed["template_metadata_events"][0][key] = value
        rejects(_validate_template_metadata, metadata_ready, metadata_events, changed)
    for key in TEMPLATE_METADATA_FIELDS:
        changed = json.loads(json.dumps(metadata_summary))
        del changed["template_metadata_events"][1][key]
        rejects(_validate_template_metadata, metadata_ready, metadata_events, changed)
    for key, value in (("event_stream_complete", False), ("event_loss_detected", True),
                       ("global_incomplete_reasons", ["missing_event"]), ("stop_seen", False),
                       ("bus_dispatch_count", 1), ("bus_dispatch_count", 4097),
                       ("bus_dispatch_count", True), ("ended_monotonic_ns", 299)):
        rejects(_validate_template_metadata, metadata_ready, metadata_events,
                {**metadata_summary, key: value})
    rejects(_validate_template_metadata, metadata_ready, [], metadata_summary)
    rejects(_validate_template_metadata, metadata_ready,
            [{**metadata_events[0], "unit": TEMPLATE_METADATA_UNIT}], metadata_summary)
    rejects(_validate_template_metadata, metadata_ready,
            [{**metadata_events[0], "monotonic_ns": 300}], metadata_summary)
    def journal_fixture(records: list[dict[str, Any]]) -> tuple[bytes, dict[str, Any]]:
        raw = b"".join(json.dumps(record).encode() + b"\n" for record in records)
        return raw, {"journal_capture": {"bytes": len(raw), "records": len(records),
                                          "sha256": hashlib.sha256(raw).hexdigest()}}
    canonical_record = {"UNIT": metadata_events[0]["unit"], "MESSAGE": "synthetic fixture"}
    journal_raw, journal_expected = journal_fixture([canonical_record])
    require(_validate_journal_capture(journal_raw, journal_expected) == [canonical_record],
            "canonical journal fixture failed")
    checks += 1
    for field in JOURNAL_UNIT_FIELDS:
        for value in (TEMPLATE_METADATA_UNIT, [TEMPLATE_METADATA_UNIT], None, 1, {},
                      list(TEMPLATE_METADATA_UNIT.encode())):
            raw, expectation = journal_fixture([canonical_record, {field: value}])
            rejects(_validate_journal_capture, raw, expectation)
    rejects(_validate_journal_capture, journal_raw + b" ", journal_expected)
    rejects(_validate_journal_capture, journal_raw,
            {"journal_capture": {**journal_expected["journal_capture"], "records": 2}})

    # Two retained full witnesses must both agree; a valid first one cannot
    # erase a later contradictory socket or security observation.
    diagnostic_profile = {"socket": {"ino": 12345, "dev": 8, "mode": stat.S_IFSOCK | 0o777},
        "status": sequence["status"], "cgroup_path": "/synthetic-broker",
        "ro_mounts": {"/": True}}
    capture = {"start_ticks": 5812,
        "socket": {"inode": 12345, "device": 8, "mode": stat.S_IFSOCK | 0o777},
        "proc": {"status": sequence["status"], "cgroup_path": "/synthetic-broker",
                 "ro_mounts": {"/": True}}}
    second = json.loads(json.dumps(capture))
    _check_complete_capture_conservation([capture, second], diagnostic_profile, 5812, "fixture")
    checks += 1
    second["socket"]["inode"] = 54321
    rejects(_check_complete_capture_conservation, [capture, second], diagnostic_profile, 5812, "fixture")
    rejects(_check_complete_capture_conservation, [second, capture], diagnostic_profile, 5812, "fixture")
    second = json.loads(json.dumps(capture))
    second["proc"]["status"]["Seccomp"] = "0"
    rejects(_check_complete_capture_conservation, [capture, second], diagnostic_profile, 5812, "fixture")
    rejects(_check_complete_capture_conservation, [], diagnostic_profile, 5812, "fixture")

    # Retained failure is a manager observation, never a successful request.
    # Synthetic fields exercise the real raw-ref and PID1/C join adapters.
    unit = "buster-bench-systemd-broker@5-789-0.service"
    invocation = "a" * 32
    unit_path = _unit_object_path(unit)
    positive = {"boot_id": expected["boot_id"], "boot_id_raw": boot_raw,
        "unit": unit, "object_path": unit_path, "generation": 1,
        "invocation_id": invocation, "main_pid": 123, "start_ticks": 99,
        "exec_main_start_timestamp_monotonic": 1000,
        "cgroup": expected["profile"]["cgroup_prefix"] + unit,
        "complete": True, "manager_properties_complete": True,
        "process_capture_complete": True, "finished_monotonic_ns": 1_500_000}
    unit_row = {key: positive[key] for key in ("boot_id", "boot_id_raw", "unit", "object_path",
        "generation", "invocation_id", "main_pid", "start_ticks")}
    unit_row.update(snapshots=[positive], last_event_seq=3, process_started=True,
                    process_start_seen=True, manager_lifecycle_complete=True)
    phase = {"manager_sender": ":1.0", "ready_monotonic_ns": 100_000,
        "stop_monotonic_ns": 4_000_000, "terminal_phase_started_monotonic_ns": 4_000_000,
        "terminal_phase_finished_monotonic_ns": 6_000_000, "ended_monotonic_ns": 7_000_000,
        "manager_observation_complete": True}
    values = {"Id": unit, "LoadState": "loaded", "CollectMode": "inactive", "ActiveState": "failed",
        "SubState": "failed", "MainPID": "0", "InvocationID": invocation, "ExecMainPID": "123",
        "ExecMainCode": "1", "ExecMainStatus": "1", "ExecMainStartTimestampMonotonic": "1000",
        "ExecMainExitTimestampMonotonic": "2000", "Result": "exit-code"}
    terminal = {key: positive[key] for key in ("boot_id", "boot_id_raw", "unit", "object_path", "generation")}
    terminal.update(manager_sender=":1.0", event_seq=3, started_monotonic_ns=5_000_000,
        finished_monotonic_ns=5_100_000, raw_show=None, raw_stderr=None,
        systemctl_exit=0, systemctl_timed_out=False, systemctl_timeout_reason=None,
        systemctl_cancelled=None, manager_properties_complete=True, incomplete_reasons=[], properties=values)
    unit_row["retained_failed_terminal"] = terminal
    life_events = [{"seq": 3, "monotonic_ns": 3_000_000}]
    require(_retained_terminal_shape(unit_row, phase, life_events, None), "terminal shape positive failed")
    checks += 1
    for key, value in (("Id", unit + "x"), ("LoadState", "not-found"), ("CollectMode", "inactive-or-failed"),
                       ("ActiveState", "active"), ("SubState", "running"), ("MainPID", "123"),
                       ("InvocationID", "b" * 32), ("ExecMainPID", "124"), ("ExecMainCode", "2"),
                       ("ExecMainStatus", "2"), ("ExecMainStartTimestampMonotonic", "999"),
                       ("ExecMainExitTimestampMonotonic", "1000"), ("Result", "success")):
        changed = json.loads(json.dumps(unit_row))
        changed["retained_failed_terminal"]["properties"][key] = value
        rejects(_retained_terminal_shape, changed, phase, life_events, None)
    changed = json.loads(json.dumps(unit_row))
    changed["snapshots"][0]["complete"] = False
    rejects(_retained_terminal_shape, changed, phase, life_events, None)
    changed = json.loads(json.dumps(unit_row))
    changed["last_event_seq"] = 4
    rejects(_retained_terminal_shape, changed, phase, life_events + [{"seq": 4, "monotonic_ns": 5_200_000}], None)
    removed = {"seq": 4, "monotonic_ns": 5_200_000}
    require(_retained_terminal_shape(changed, phase, life_events + [removed], removed),
            "completed terminal before later removal did not preserve removal path")
    checks += 1
    rejects(_retained_terminal_shape, changed, phase, life_events + [removed],
            {"seq": 4, "monotonic_ns": 5_050_000})
    for key, value in (("stop_monotonic_ns", 5_500_000),
                       ("terminal_phase_finished_monotonic_ns", 31_000_000_000)):
        rejects(_retained_terminal_shape, unit_row, {**phase, key: value}, life_events, None)
    changed = json.loads(json.dumps(unit_row))
    changed["retained_failed_terminal"]["systemctl_timed_out"] = True
    rejects(_retained_terminal_shape, changed, phase, life_events, None)

    journal_identity = {"_PID": "1", "_TRANSPORT": "journal", "_BOOT_ID": expected["boot_id"],
                        "UNIT": unit, "INVOCATION_ID": invocation}
    records = [
        {**journal_identity, "MESSAGE_ID": STARTED_MESSAGE_ID, "JOB_TYPE": "start",
         "JOB_RESULT": "done", "__MONOTONIC_TIMESTAMP": "1100"},
        {**journal_identity, "MESSAGE_ID": PROCESS_EXIT_MESSAGE_ID, "COMMAND": "ExecStart",
         "EXIT_CODE": "exited", "EXIT_STATUS": "1", "__MONOTONIC_TIMESTAMP": "2050"},
        {**journal_identity, "MESSAGE_ID": UNIT_FAILED_MESSAGE_ID, "UNIT_RESULT": "exit-code",
         "__MONOTONIC_TIMESTAMP": "2060"}]
    failed_rows = collect_manager_failed_exit_rows(records, expected["boot_id"])
    require(len(failed_rows) == 1, "PID1 failure index positive failed")
    checks += 1
    rejects(collect_manager_failed_exit_rows, records[:2], expected["boot_id"])
    rejects(collect_manager_failed_exit_rows, records + [records[-1]], expected["boot_id"])
    rejects(collect_manager_failed_exit_rows, [records[0], records[2], records[1]], expected["boot_id"])
    for key, value in (("_PID", "2"), ("_TRANSPORT", "stdout"), ("_BOOT_ID", "b" * 32),
                       ("INVOCATION_ID", "b" * 32), ("__MONOTONIC_TIMESTAMP", "1000")):
        changed_records = json.loads(json.dumps(records))
        changed_records[1][key] = value
        rejects(collect_manager_failed_exit_rows, changed_records, expected["boot_id"])
    manager = {"result": "done", "record_number": 1, "invocation_id": invocation}
    diagnostic = {"sequence": {"pid": 123, "start_ticks": 99,
                   "outcome": {"broker_exit": 1, "frame_status": 126, "frame_sent": 1}}}
    with tempfile.TemporaryDirectory(prefix="issue1507-consumer-") as directory:
        raw_values = {**values, "ControlGroup": "", "ProtectSystem": "strict",
                      "ReadOnlyPaths": " ".join(expected["profile"]["read_only_mount_targets"][1:])}
        stdout = "".join(f"{key}={value}\n" for key, value in raw_values.items()).encode()
        def fixture_ref(name: str, data: bytes) -> dict[str, Any]:
            (Path(directory) / name).write_bytes(data)
            return {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                    "exit": 0, "timed_out": False}
        terminal["raw_show"] = fixture_ref("terminal.txt", stdout)
        terminal["raw_stderr"] = fixture_ref("terminal.stderr", b"")
        descriptor = open_absolute_directory(directory)
        try:
            _validate_retained_terminal_join(unit_row, manager, diagnostic, failed_rows, expected, descriptor)
            checks += 1
            rejects(_validate_retained_terminal_join, unit_row, manager, diagnostic, {}, expected, descriptor)
            changed_diagnostic = json.loads(json.dumps(diagnostic))
            changed_diagnostic["sequence"]["outcome"]["broker_exit"] = 0
            rejects(_validate_retained_terminal_join, unit_row, manager, changed_diagnostic,
                    failed_rows, expected, descriptor)
            changed = json.loads(json.dumps(unit_row))
            changed["retained_failed_terminal"]["raw_show"]["sha256"] = "0" * 64
            rejects(_validate_retained_terminal_join, changed, manager, diagnostic, failed_rows, expected, descriptor)
            changed = json.loads(json.dumps(unit_row))
            altered = stdout.replace(b"ExecMainPID=123\n", b"ExecMainPID=124\n")
            changed["retained_failed_terminal"]["raw_show"] = fixture_ref("altered.txt", altered)
            rejects(_validate_retained_terminal_join, changed, manager, diagnostic, failed_rows, expected, descriptor)
            changed_failures = json.loads(json.dumps(next(iter(failed_rows.values()))))
            changed_failures["exit"]["status"] = "2"
            rejects(_validate_retained_terminal_join, unit_row, manager, diagnostic,
                    {next(iter(failed_rows)): changed_failures}, expected, descriptor)
        finally:
            os.close(descriptor)
    skipped = {"boot_id": expected["boot_id"], "unit": unit, "object_path": unit_path, "generation": 1,
        "trigger_event_seq": 3, "started_monotonic_ns": 5_300_000, "finished_monotonic_ns": 5_300_001,
        "post_removal_unstarted": True, "incomplete_reasons": ["unit_removed_before_snapshot"],
        "complete": False, "manager_properties_complete": False, "process_capture_complete": False,
        "main_pid": 0, "process_started": False, "proc_capture": {}, "systemctl_timed_out": False}
    require(_post_removal_unstarted(skipped, unit_row, removed), "never-started redundant query fixture failed")
    checks += 1
    rejects(_post_removal_unstarted, {**skipped, "raw_show": {"path": "not-empty"}}, unit_row, removed)
    rejects(_post_removal_unstarted, {**skipped, "systemctl_exit": 0}, unit_row, removed)
    for key, value in (("result", "success"), ("active_state", "active"), ("substate", "running"),
                       ("exec_main_status", "0"), ("invocation_id_observed", invocation),
                       ("exec_main_start_timestamp_monotonic", 1),
                       ("process_capture_state", "full_process_witness"),
                       ("initial_exe_observed", "/usr/local/libexec/buster-benchmark-broker"),
                       ("proc_cgroup_raw", "0::/system.slice/example"),
                       ("preliminary_property_miss", True),
                       ("preliminary_property_miss_reason", "unit_new_before_process_start"),
                       ("main_pid", False)):
        rejects(_post_removal_unstarted, {**skipped, key: value}, unit_row, removed)
    rejects(_post_removal_unstarted, {**skipped, "trigger_event_seq": 4}, unit_row, removed)
    require(not _post_removal_unstarted(skipped, {**unit_row, "snapshots": []}, removed),
            "never-started query without earlier full witness was accepted")
    checks += 1

    print(f"issue1162 broker evidence self-test: {checks} checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvidenceError as exc:
        print(json.dumps({"schema": "issue1162-broker-evidence-result-v1",
                          "full_ready": False, "errors": [str(exc)]}, sort_keys=True), file=sys.stderr)
        raise SystemExit(2)
