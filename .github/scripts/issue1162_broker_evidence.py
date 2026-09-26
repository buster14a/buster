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
import zlib
from typing import Any

SCHEMA = "issue1162-broker-evidence-expectation-v1"
OBS_SCHEMA = "issue1162-broker-observer-v1"
MAX_FILE = 128 * 1024 * 1024
MAX_JSON_LINE = 4 * 1024 * 1024
MAX_INSTANCES = 128
MAX_DIAG_BYTES = 512 * 1024
MAX_DIAG_LINE = 1200
DATA_FIELDS = ("stat", "status", "mountinfo", "cgroup", "exe", "socket")
DATA_LIMITS = {"stat": 4096, "status": 16384, "mountinfo": 131072,
               "cgroup": 4096, "exe": 767, "socket": 255}
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
            if name in ("peer_pid", "operation", "stage", "job", "attempt"):
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
    for record_number, record in enumerate(records, 1):
        unit_value = record.get("UNIT")
        if not isinstance(unit_value, str) or UNIT_RE.fullmatch(unit_value) is None:
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
        invocation = record.get("INVOCATION_ID")
        if invocation is not None:
            require(isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None,
                    f"manager start record {record_number} has malformed invocation ID")
        require(key not in rows, f"duplicate terminal manager start record for {unit_value}")
        rows[key] = {"boot_id": boot, "unit": unit_value, "result": result,
                     "invocation_id": invocation, "message_id": message_id,
                     "record_number": record_number}
    require(set(progress) <= set(rows), "manager Starting record lacks a terminal start result")
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
    def replace(match: re.Match[str]) -> str:
        return chr(int(match.group(1), 8))
    return re.sub(r"\\([0-7]{3})", replace, value)


def _readonly_mounts(raw: bytes, targets: list[str], label: str) -> dict[str, bool]:
    require(raw.endswith(b"\n") and b"\x00" not in raw, f"{label} mountinfo terminator invalid")
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label} mountinfo is not ASCII") from exc
    mounts: dict[str, list[tuple[int, set[str]]]] = {}
    mount_ids: set[int] = set()
    for line in lines:
        columns = line.split()
        separator = columns.index("-") if "-" in columns else -1
        require(separator >= 6 and len(columns) >= separator + 4,
                f"{label} mountinfo record is malformed")
        mount_id = canonical_uint(columns[0], f"{label} mount ID", (1 << 31) - 1, False)
        require(mount_id not in mount_ids, f"{label} duplicate mount ID")
        mount_ids.add(mount_id)
        mountpoint = _unescape_mount_path(columns[4])
        require(mountpoint.startswith("/") and "\x00" not in mountpoint,
                f"{label} mountpoint is malformed")
        options = set(columns[5].split(","))
        require(("ro" in options) != ("rw" in options),
                f"{label} mountinfo has ambiguous read/write options")
        mounts.setdefault(mountpoint, []).append((mount_id, options))
    result: dict[str, bool] = {}
    for target in targets:
        matches = mounts.get(target, [])
        require(bool(matches), f"{label} required mount target is absent: {target}")
        active_id, active_options = max(matches, key=lambda item: item[0])
        del active_id
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


def _safe_ref(root_fd: int, ref: Any, label: str, maximum: int) -> bytes:
    require(isinstance(ref, dict) and set(ref) == {"path", "sha256", "bytes", "exit", "timed_out"},
            f"{label} reference has unknown or missing fields")
    require(isinstance(ref["path"], str) and isinstance(ref["sha256"], str) and
            HEX64.fullmatch(ref["sha256"]) is not None and type(ref["bytes"]) is int and
            0 <= ref["bytes"] <= maximum and ref["exit"] == 0 and
            ref["timed_out"] is False,
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
    required = {"Id", "LoadState", "InvocationID", "MainPID", "ExecMainPID", "ControlGroup",
                "ExecMainStartTimestampMonotonic", "ExecMainStatus", "Result", "SubState"}
    require(required <= properties.keys(), f"systemctl show lacks required identity for {unit}")
    require(properties["Id"] == unit and properties["LoadState"] == "loaded",
            f"systemctl show unit/load state mismatch for {unit}")
    show_invocation = properties["InvocationID"] or None
    require(show_invocation is None or HEX32.fullmatch(show_invocation) is not None,
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
    for key in ("result", "substate", "exec_main_status"):
        prop = {"result": "Result", "substate": "SubState", "exec_main_status": "ExecMainStatus"}[key]
        require(isinstance(snapshot.get(key), str) and snapshot[key] == properties[prop],
                f"systemctl show {prop} differs from snapshot for {unit}")
    require(re.fullmatch(r"0|[1-9][0-9]*", properties["ExecMainStartTimestampMonotonic"]) is not None,
            f"systemctl show monotonic start timestamp malformed for {unit}")
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
            (isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None),
            f"snapshot invocation ID is malformed for {unit}")
    require(not snapshot["process_started"] or invocation is not None,
            f"started process lacks snapshot invocation ID for {unit}")
    if not snapshot["complete"]:
        return {"complete": False, "reason": snapshot["incomplete_reasons"]}
    require(snapshot.get("systemctl_cancelled") is None and
            snapshot.get("systemctl_exit") == 0 and snapshot.get("systemctl_timed_out") is False,
            f"completed systemctl attempt has exit/timeout/cancel failure for {unit}")
    show_raw = _safe_ref(observer_fd, snapshot.get("raw_show"), f"{unit} systemctl show", 4 * 1024 * 1024)
    stderr_raw = _safe_ref(observer_fd, snapshot.get("raw_stderr"), f"{unit} systemctl stderr", 1024 * 1024)
    require(snapshot["raw_show"]["exit"] == snapshot["systemctl_exit"] and
            snapshot["raw_stderr"]["exit"] == snapshot["systemctl_exit"] and
            snapshot["raw_show"]["timed_out"] == snapshot["systemctl_timed_out"] and
            snapshot["raw_stderr"]["timed_out"] == snapshot["systemctl_timed_out"] and
            not stderr_raw.strip(), f"systemctl raw references disagree or stderr is nonempty for {unit}")
    properties = _parse_systemctl_show(show_raw, unit, invocation, snapshot, expected)
    verified: dict[str, Any] = {"complete": True, "properties": properties,
                                "main_pid": snapshot["main_pid"], "start_ticks": snapshot["start_ticks"],
                                "proc": None, "socket": None}
    if snapshot["process_started"]:
        require(snapshot.get("exe") == expected["broker_executable_path"] and
                snapshot.get("exe_sha256") == expected["broker_binary_sha256"] and
                isinstance(snapshot.get("cgroup"), str) and
                properties["ControlGroup"] == snapshot["cgroup"],
                f"process executable/hash/cgroup identity differs for {unit}")
        proc_refs = snapshot.get("proc_capture")
        require(isinstance(proc_refs, dict) and
                set(proc_refs) == {"proc-status", "proc-mountinfo", "proc-cgroup", "fd0_link", "fd0_info"},
                f"process proc references are incomplete for {unit}")
        status_raw = _safe_ref(observer_fd, proc_refs["proc-status"], f"{unit} proc status", 1024 * 1024)
        mount_raw = _safe_ref(observer_fd, proc_refs["proc-mountinfo"], f"{unit} proc mountinfo", 16 * 1024 * 1024)
        cgroup_raw = _safe_ref(observer_fd, proc_refs["proc-cgroup"], f"{unit} proc cgroup", 1024 * 1024)
        status = _check_profile_status(status_raw, snapshot["main_pid"], expected["profile"], f"{unit} external")
        cgroup_path = _parse_cgroup(cgroup_raw, expected["profile"]["cgroup_prefix"], f"{unit} external")
        require(cgroup_path == properties["ControlGroup"] and
                snapshot["cgroup"] == cgroup_path,
                f"external cgroup does not match full manager show for {unit}")
        ro_mounts = _readonly_mounts(mount_raw, expected["profile"]["read_only_mount_targets"],
                                     f"{unit} external")
        _validate_fd0(snapshot, proc_refs, observer_fd, None, unit)
        verified.update({"proc": {"status_raw": status_raw, "status": status,
                                   "mountinfo_raw": mount_raw, "ro_mounts": ro_mounts,
                                   "cgroup_raw": cgroup_raw, "cgroup_path": cgroup_path},
                         "socket": snapshot["socket_fd0"]})
    else:
        require(snapshot.get("start_ticks") is None and snapshot.get("proc_capture") in (None, {}) and
                snapshot.get("socket_fd0") is None,
                f"no-PID snapshot contains process captures for {unit}")
    return verified


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
    events = parse_jsonl(read_regular_at(observer_fd, "events.jsonl", MAX_FILE), "observer events.jsonl")
    summary = strict_json(read_regular_at(observer_fd, "summary.json", 16 * 1024 * 1024), "observer summary.json")
    require(isinstance(ready, dict) and ready.get("schema") == OBS_SCHEMA and ready.get("kind") == "READY",
            "observer readiness schema mismatch")
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
            type(ready.get("ready_monotonic_ns")) is int and ready["ready_monotonic_ns"] > 0 and
            type(ready.get("observer_pid")) is int and ready["observer_pid"] > 0 and
            type(ready.get("observer_start_ticks")) is int and ready["observer_start_ticks"] > 0 and
            isinstance(ready.get("manager_sender"), str) and
            re.fullmatch(r":[1-9][0-9]*\.(?:0|[1-9][0-9]*)", ready["manager_sender"]) is not None,
            "observer readiness is incomplete or bound to another run")
    require(summary.get("schema") == OBS_SCHEMA and summary.get("run_id") == expected["run_id"] and
            summary.get("run_attempt") == expected["run_attempt"] and
            summary.get("population_complete") is False and
            summary.get("event_loss_detected") is False and
            summary.get("event_stream_complete") is True,
            "observer summary reports loss, incomplete stream, or wrong run")
    require(len(events) <= 100_000, "observer event count exceeds bound")
    for number, event in enumerate(events, 1):
        require(event.get("schema") == OBS_SCHEMA and event.get("boot_id") == expected["boot_id"] and
                event.get("boot_id_raw") == expected["boot_id_raw"] and
                type(event.get("seq")) is int and event.get("seq") == number and
                type(event.get("monotonic_ns")) is int and event["monotonic_ns"] > 0,
                f"observer event {number} has a sequence or boot gap")
    return ready, events, summary


def _generation_key(row: dict[str, Any]) -> tuple[str, str, str, int]:
    boot = row.get("boot_id")
    unit = row.get("unit")
    path = row.get("object_path")
    generation = row.get("generation")
    require(isinstance(boot, str) and HEX32.fullmatch(boot) is not None and
            isinstance(path, str) and len(path) <= 512 and
            path.startswith("/org/freedesktop/systemd1/unit/") and path.count("/") == 6 and
            ".." not in path.split("/") and type(generation) is int and generation > 0,
            "observer lifecycle identity is malformed")
    parse_broker_unit(unit, "observer lifecycle unit")
    return boot, unit, path, generation


def _validate_event_rows(ready: dict[str, Any], events: list[dict[str, Any]],
                         summary: dict[str, Any]) -> tuple[dict[tuple[str, str, str, int], dict[str, Any]],
                                                          list[dict[str, Any]]]:
    ready_ns = ready["ready_monotonic_ns"]
    lifecycles: dict[tuple[str, str, str, int], dict[str, Any]] = {}
    last_ns = ready_ns
    for event in events:
        require(boot_uuid(event.get("boot_id_raw"), "event boot_id_raw") == event["boot_id"],
                "observer event raw boot UUID differs")
        key = _generation_key(event)
        require(event["monotonic_ns"] >= last_ns, "observer monotonic event order regressed")
        last_ns = event["monotonic_ns"]
        require(event.get("sender") == ready["manager_sender"] and
                isinstance(event.get("member"), str), "observer event sender/member is missing")
        kind = event.get("event")
        if kind == "UnitNew":
            require(event.get("member") == "UnitNew" and
                    event.get("interface") == "org.freedesktop.systemd1.Manager" and
                    key not in lifecycles, "duplicate or malformed UnitNew event")
            lifecycles[key] = {"new": event, "properties": [], "removed": None}
        elif kind == "PropertiesChanged":
            require(event.get("member") == "PropertiesChanged" and
                    event.get("interface") == "org.freedesktop.DBus.Properties" and
                    isinstance(event.get("unit_interface"), str) and
                    isinstance(event.get("changed_properties"), list) and
                    len(event["changed_properties"]) == len(set(event["changed_properties"])) and
                    all(isinstance(item, str) and item for item in event["changed_properties"]) and
                    isinstance(event.get("invalidated_properties"), list) and
                    all(isinstance(item, str) and item for item in event["invalidated_properties"]) and
                    len(event["invalidated_properties"]) == len(set(event["invalidated_properties"])) and
                    isinstance(event.get("main_pids"), list) and
                    all(type(item) is int and 0 <= item <= (1 << 31) - 1 for item in event["main_pids"]),
                    "malformed PropertiesChanged event")
            require(key in lifecycles and lifecycles[key]["removed"] is None,
                    "PropertiesChanged is outside its observed generation")
            lifecycles[key]["properties"].append(event)
        elif kind == "UnitRemoved":
            require(event.get("member") == "UnitRemoved" and
                    event.get("interface") == "org.freedesktop.systemd1.Manager" and
                    key in lifecycles and lifecycles[key]["removed"] is None,
                    "duplicate or unpaired UnitRemoved event")
            lifecycles[key]["removed"] = event
        else:
            raise EvidenceError("observer has an unknown lifecycle event")
    units = summary.get("units")
    snapshots = summary.get("snapshots")
    require(isinstance(units, list) and isinstance(snapshots, list) and
            len(units) <= MAX_INSTANCES and len(snapshots) <= 100_000,
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
        require(invocation is None or (isinstance(invocation, str) and HEX32.fullmatch(invocation) is not None),
                f"observer invocation ID is malformed for {key[1]}")
        main_pid = unit_row.get("main_pid")
        started = unit_row.get("process_started")
        ticks = unit_row.get("start_ticks")
        require(type(main_pid) is int and 0 <= main_pid <= (1 << 31) - 1 and
                type(started) is bool and started == (main_pid > 0) and
                ((started and type(ticks) is int and ticks > 0 and invocation is not None) or
                 (not started and ticks is None)),
                f"observer process identity is malformed for {key[1]}")
        require(type(unit_row.get("complete")) is bool and
                isinstance(unit_row.get("incomplete_reasons"), list) and
                all(isinstance(item, str) and item for item in unit_row["incomplete_reasons"]),
                f"observer unit completeness is malformed for {key[1]}")
        row_snaps = unit_row.get("snapshots")
        require(isinstance(row_snaps, list), f"observer unit snapshot list missing for {key[1]}")
        for snapshot in row_snaps:
            require(isinstance(snapshot, dict) and _generation_key(snapshot) == key,
                    f"nested observer snapshot identity mismatch for {key[1]}")
            require(isinstance(snapshot.get("boot_id_raw"), str) and
                    boot_uuid(snapshot["boot_id_raw"], "snapshot boot_id_raw") == key[0],
                    f"snapshot raw boot ID differs for {key[1]}")
            nested_snapshots.append(snapshot)
        unit_rows[key] = unit_row
    require(set(unit_rows) == set(lifecycles),
            "observer summary omitted or invented a manager lifecycle generation")
    nested_sorted = sorted((json.dumps(row, sort_keys=True, separators=(",", ":")) for row in nested_snapshots))
    top_sorted = sorted((json.dumps(row, sort_keys=True, separators=(",", ":")) for row in snapshots))
    require(nested_sorted == top_sorted, "observer top-level snapshots differ from per-generation snapshots")
    for snapshot in snapshots:
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
        if snapshot["complete"]:
            require(not snapshot["incomplete_reasons"],
                    f"snapshot claims complete with incomplete reasons for {key[1]}")
        else:
            require(bool(snapshot["incomplete_reasons"]),
                    f"incomplete snapshot lacks a reason for {key[1]}")
        main_pid = snapshot.get("main_pid")
        started = snapshot.get("process_started")
        ticks = snapshot.get("start_ticks")
        require(type(main_pid) is int and 0 <= main_pid <= (1 << 31) - 1 and
                type(started) is bool and started == (main_pid > 0) and
                ((started and type(ticks) is int and ticks > 0 and
                  isinstance(snapshot.get("invocation_id"), str) and
                  HEX32.fullmatch(snapshot["invocation_id"]) is not None) or
                 (not started and ticks is None)),
                f"snapshot process identity is malformed for {key[1]}")
        require(snapshot.get("invocation_id") is None or
                (isinstance(snapshot.get("invocation_id"), str) and
                 HEX32.fullmatch(snapshot["invocation_id"]) is not None),
                f"snapshot invocation ID is malformed for {key[1]}")
    return unit_rows, snapshots


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
            65000 in profile["client_uids"] and 0 not in profile["client_uids"] and
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
    print(json.dumps({"schema": "issue1162-broker-evidence-result-v1",
                      "full_ready": False, "diagnostic_complete": False,
                      "manager_capture_complete": False, "external_proc_complete": False,
                      "overlap_complete": False,
                      "errors": ["caller schema integration is pending"]}, sort_keys=True))
    return 2


def self_test() -> int:
    print("issue1162 broker evidence self-test: parser fixtures pending")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except EvidenceError as exc:
        print(json.dumps({"schema": "issue1162-broker-evidence-result-v1",
                          "full_ready": False, "errors": [str(exc)]}, sort_keys=True), file=sys.stderr)
        raise SystemExit(2)
