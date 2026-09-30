#!/usr/bin/env python3
"""Emit the #509 required-check authority from a reviewed plan and verified inputs.

Usage: retirement_required_checks.py --plan PLAN --checks-dir CHECKS
       --hosted-record RECORD --output OUTPUT [--census CENSUS]

This is the production generator of
``recipes/native-retirement-performance-v1.required-checks`` (the profile
pins it with ``required-checks-sha256=``), in exactly the canonical text
``bq_retirement_required_checks_import`` (retirement_correctness_service.c)
decodes: ``BQ-RETIREMENT-REQUIRED-CHECKS-V1``, the joined ``support=``,
``census=``, ``population=`` and ``native-target=`` facts, ``hosted=``, the
``tools=``/``tool=`` pins and one ``check=``/``configuration=``/``argv=``/
``environment=`` block per check.

Inputs and what is verified here:

* PLAN (JSON, ``PLAN_SCHEMA``): the sealed projection's facts (support and
  census-rows pins, population seal, native target, row, object-row and
  compiler-eligible-row counts), A's candidate commit and tree, the tool
  names in pin order and the checks. Every field is checked with the
  importer's own bounds, token grammar and environment ordering, and the
  whole set against its coverage rules (every kind, the six #509 hosts,
  a native semantic lane, honest evidence labels, row counts).
* CHECKS (the directory installed as
  ``recipes/native-retirement-performance-v1.checks``): each named tool is
  hashed here and must be a regular ELF executable; the directory holds no
  other entry.
* RECORD (``BQ-RETIREMENT-HOSTED-ACCEPTANCE-V1``, provisional schema): the
  hosted acceptance record, consumed, never produced here. It must name the
  plan's candidate commit and tree and hold ``lane=<target> passed`` for
  every hosted check; its digest becomes ``hosted=`` and each hosted check's
  pinned output.
* CENSUS (optional): when given, the support and census-rows pins must be
  its ``support.tsv`` and ``rows.tsv`` digests.

The population seal is the C projection's (``bq_retirement_oracle_population_hash``)
and cannot be recomputed here; the importer re-derives it and refuses a
mismatch. A refusal prints a message and exits 1; OUTPUT is created
exclusively and never replaced.

Map: ``PLAN_SCHEMA``, ``KINDS``, ``EVIDENCE``, ``NATIVE_HOSTS``; ``field``
(the token grammar), ``environment_name``; ``tools``; ``hosted_record``;
``check_block``; ``coverage``; ``authority``; ``main``.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

HEADER = "BQ-RETIREMENT-REQUIRED-CHECKS-V1"
HOSTED_HEADER = "BQ-RETIREMENT-HOSTED-ACCEPTANCE-V1"
PLAN_SCHEMA = "bq-retirement-required-checks-plan-v1"
# bq_retirement_check_kind_names and bq_retirement_check_evidence_names.
KINDS = ("census", "semantic", "matrix", "no-fallback", "self-host", "fixed-point")
EVIDENCE = ("native", "emulated", "compile-only", "link-only", "hosted")
# bq_retirement_509_native_hosts: Linux, macOS and Windows on x86-64 and AArch64.
NATIVE_HOSTS = (11, 5, 8, 2, 10, 4)
# retirement_correctness_service.h and retirement_correctness.h bounds.
TARGET_MAX = 12
TOOLS_CAP = 16
TOOL_BYTES_CAP = 512 * 1024 * 1024
OUTPUT_NAME_CAP = 128
CHECKS_CAP = 256
ARGUMENTS_CAP = 64
ENVIRONMENT_CAP = 32
FIELD_CAP = 4096
TIMEOUT_MAX_SECONDS = 3500
MEMORY_MIN_MIB = 16
MEMORY_MAX_MIB = 1024 * 1024
HOSTED_BYTES_CAP = 1024 * 1024
AUTHORITY_BYTES_CAP = 4 * 1024 * 1024
FIXED_TOKENS = ("{{binary:0}}", "{{binary:1}}", "{{source:0}}", "{{source:1}}", "{{work}}")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
REVISION_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
TOOL_TOKEN_RE = re.compile(r"\{\{tool:(0|[1-9][0-9]?)\}\}")
ENVIRONMENT_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class ChecksError(Exception):
    """A plan, tool or record the importer would refuse; nothing is written."""


def _fail(message):
    raise ChecksError(message)


def _json_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            _fail(f"duplicate JSON object key: {key!r}")
        value[key] = item
    return value


def _keys(value, required, name, optional=()):
    if type(value) is not dict:
        _fail(f"{name} must be an object")
    missing = set(required) - set(value)
    unknown = set(value) - set(required) - set(optional)
    if missing or unknown:
        _fail(f"{name} fields differ: missing {sorted(missing)}, unknown {sorted(unknown)}")
    return value


def _integer(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        _fail(f"{name} must be an integer in {minimum}..{maximum}")
    return value


def _sha(value, name):
    if type(value) is not str or not SHA256_RE.fullmatch(value):
        _fail(f"{name} must be a lowercase SHA-256 digest")
    return value


def _token(text, tool_count):
    """(length, kind) of the token opening text, or (0, None)
    (bq_retirement_check_token)."""
    found = (0, None)
    for fixed in FIXED_TOKENS:
        if text.startswith(fixed):
            found = (len(fixed), fixed[2:fixed.index(":") if ":" in fixed else -2])
    match = TOOL_TOKEN_RE.match(text) if not found[0] else None
    if match and int(match.group(1)) < tool_count:
        found = (match.end(), "tool")
    return found


def field(text, tool_count, name, allow_empty=False):
    """A printable-ASCII field of at most FIELD_CAP bytes holding at most one
    token, where "{{" only ever opens a valid token (bq_retirement_check_field)."""
    if type(text) is not str or len(text.encode("utf-8")) > FIELD_CAP or (not text and not allow_empty):
        _fail(f"{name} must be a string of at most {FIELD_CAP} bytes" + ("" if allow_empty else ", not empty"))
    tokens, offset = 0, 0
    while offset < len(text):
        if not " " <= text[offset] <= "~":
            _fail(f"{name} holds a byte outside printable ASCII")
        length = _token(text[offset:], tool_count)[0] if text.startswith("{{", offset) else 0
        if text.startswith("{{", offset) and not length:
            _fail(f"{name} opens an unknown token at offset {offset}")
        tokens += length != 0
        offset += length or 1
    if tokens > 1:
        _fail(f"{name} holds more than one token")
    return text


def environment_name(entry):
    """The NAME of a NAME=value entry (bq_retirement_check_environment_name)."""
    name, separator, _value = entry.partition("=") if type(entry) is str else ("", "", "")
    if not separator or not ENVIRONMENT_NAME_RE.fullmatch(name):
        _fail(f"environment entry {entry!r} is not NAME=value")
    return name


def tool_name(name, index):
    if type(name) is not str or not 0 < len(name) < OUTPUT_NAME_CAP or name in (".", "..") or \
            any(not "!" <= character <= "~" or character == "/" for character in name):
        _fail(f"plan.tools[{index}] is not one printable path component")
    return name


def tools(directory, names):
    """(name, sha256) of every pinned tool: a regular ELF file directly under
    directory, which holds nothing else."""
    if type(names) is not list or not 1 <= len(names) <= TOOLS_CAP:
        _fail(f"plan.tools must name 1..{TOOLS_CAP} tools")
    names = [tool_name(name, index) for index, name in enumerate(names)]
    if len(set(names)) != len(names):
        _fail("plan.tools repeats a name")
    directory = Path(directory)
    if not stat.S_ISDIR(os.lstat(directory).st_mode):
        _fail("the checks directory is not a directory")
    present = sorted(os.listdir(directory))
    if present != sorted(names):
        _fail(f"the checks directory holds {present}, not exactly the pinned tools {sorted(names)}")
    pinned = []
    for name in names:
        metadata = os.lstat(directory / name)
        if not stat.S_ISREG(metadata.st_mode) or not metadata.st_mode & stat.S_IXUSR:
            _fail(f"tool {name} is not a regular executable file")
        with open(directory / name, "rb") as stream:
            data = stream.read(TOOL_BYTES_CAP + 1)
        if len(data) > TOOL_BYTES_CAP or not data.startswith(b"\x7fELF"):
            _fail(f"tool {name} is not an ELF executable within {TOOL_BYTES_CAP} bytes")
        pinned.append((name, hashlib.sha256(data).hexdigest()))
    return pinned


def hosted_record(data, candidate, hosted_targets):
    """The record's digest after binding it to A's candidate and every hosted
    lane (bq_retirement_required_checks_hold_hosted)."""
    if not data or len(data) > HOSTED_BYTES_CAP or b"\0" in data:
        _fail("hosted acceptance record is empty, oversized or holds a NUL")
    header = f"{HOSTED_HEADER}\ncommit={candidate['commit']}\ntree={candidate['tree']}\n".encode()
    if not data.startswith(header):
        _fail("hosted acceptance record does not name the candidate commit and tree")
    for target in hosted_targets:
        if f"\nlane={target} passed\n".encode() not in data[len(header) - 1:]:
            _fail(f"hosted acceptance record has no passed lane for target {target}")
    return hashlib.sha256(data).hexdigest()


def check_block(index, check, projection, tool_count, hosted_sha256):
    """One check's canonical lines and its (kind, target, evidence,
    configuration) identity."""
    hosted = type(check) is dict and check.get("evidence") == "hosted"
    required = ("kind", "target", "rows", "evidence", "timeout_seconds", "memory_mib", "configuration", "argv",
                "environment")
    check = _keys(check, required if hosted else (*required, "stdout_sha256"), f"plan.checks[{index}]")
    name = f"plan.checks[{index}]"
    if check["kind"] not in KINDS or check["evidence"] not in EVIDENCE:
        _fail(f"{name} has an unknown kind or evidence label")
    target = _integer(check["target"], 0, TARGET_MAX, f"{name}.target")
    rows = _integer(check["rows"], 1, projection["rows"], f"{name}.rows")
    timeout = _integer(check["timeout_seconds"], 1, TIMEOUT_MAX_SECONDS, f"{name}.timeout_seconds")
    memory = _integer(check["memory_mib"], MEMORY_MIN_MIB, MEMORY_MAX_MIB, f"{name}.memory_mib")
    output = hosted_sha256 if hosted else _sha(check["stdout_sha256"], f"{name}.stdout_sha256")
    configuration = field(check["configuration"], 0, f"{name}.configuration")
    if "{{" in configuration:
        _fail(f"{name}.configuration holds a token")
    argv, environment = check["argv"], check["environment"]
    if type(argv) is not list or type(environment) is not list:
        _fail(f"{name}.argv and .environment must be arrays")
    if hosted and (argv or environment):
        _fail(f"{name} is hosted, so it has no argv or environment")
    if not hosted and not 1 <= len(argv) <= ARGUMENTS_CAP:
        _fail(f"{name}.argv must hold 1..{ARGUMENTS_CAP} arguments")
    if len(environment) > ENVIRONMENT_CAP:
        _fail(f"{name}.environment exceeds {ENVIRONMENT_CAP} entries")
    for position, argument in enumerate(argv):
        field(argument, tool_count, f"{name}.argv[{position}]")
    if argv:
        length, kind = _token(argv[0], tool_count)
        if length != len(argv[0]) or kind not in ("binary", "tool"):
            _fail(f"{name}.argv[0] must be exactly a binary or tool token")
    names = []
    for position, entry in enumerate(environment):
        names.append(environment_name(entry))
        if len(entry.encode("utf-8")) > FIELD_CAP:
            _fail(f"{name}.environment[{position}] exceeds {FIELD_CAP} bytes")
        field(entry[len(names[-1]) + 1:], tool_count, f"{name}.environment[{position}]", allow_empty=True)
        if position and not environment[position - 1].encode() < entry.encode():
            _fail(f"{name}.environment must be strictly ascending")
    if len(set(names)) != len(names):
        _fail(f"{name}.environment repeats a name")
    lines = [f"check={index} {check['kind']} {target} {rows} {check['evidence']} {timeout} {memory} {output}",
             f"configuration={configuration}", f"argv={len(argv)}", *(f"arg={argument}" for argument in argv),
             f"environment={len(environment)}", *(f"env={entry}" for entry in environment)]
    return lines, (check["kind"], target, check["evidence"], configuration), rows


def coverage(identities, projection):
    """bq_retirement_required_checks_cover over the decoded identities."""
    native_target = projection["native_target"]
    hosts, kinds, native_lane = set(), set(), False
    for (kind, target, evidence, _configuration), rows in identities:
        if evidence == "native" and target not in (0, native_target):
            _fail(f"a native {kind} check names target {target}, not the native target or 0")
        if evidence == "hosted" and kind != "semantic":
            _fail("only a semantic check can be hosted")
        if kind == "semantic" and target < 1:
            _fail("a semantic check names no target")
        if kind == "census" and rows != projection["object_rows"]:
            _fail("a census check must cover exactly the object rows")
        if kind in ("matrix", "no-fallback") and rows != projection["eligible_rows"]:
            _fail(f"a {kind} check must cover exactly the compiler-eligible rows")
        kinds.add(kind)
        if kind == "semantic" and target in NATIVE_HOSTS and \
                (evidence == "hosted" or (evidence == "native" and target == native_target)):
            hosts.add(target)
        native_lane = native_lane or (kind == "semantic" and target == native_target and evidence == "native")
    distinct = {identity for identity, _rows in identities}
    if len(distinct) != len(identities):
        _fail("two checks share kind, target, evidence and configuration")
    if kinds != set(KINDS):
        _fail(f"the checks omit kinds {sorted(set(KINDS) - kinds)}")
    if hosts != set(NATIVE_HOSTS):
        _fail(f"#509 hosts {sorted(set(NATIVE_HOSTS) - hosts)} are covered by no native or hosted semantic check")
    if not native_lane:
        _fail("no native semantic check on the native target")


def authority(plan, checks_directory, hosted_data, census=None):
    """The canonical authority bytes for plan."""
    plan = _keys(plan, ("schema", "projection", "candidate", "tools", "checks"), "plan")
    if plan["schema"] != PLAN_SCHEMA:
        _fail(f"plan.schema must be {PLAN_SCHEMA!r}")
    projection = _keys(plan["projection"], ("support_sha256", "census_sha256", "population_sha256", "native_target",
                                            "rows", "object_rows", "eligible_rows"), "plan.projection")
    for key in ("support_sha256", "census_sha256", "population_sha256"):
        _sha(projection[key], f"plan.projection.{key}")
    _integer(projection["native_target"], 1, TARGET_MAX, "plan.projection.native_target")
    _integer(projection["rows"], 1, 1 << 32, "plan.projection.rows")
    _integer(projection["object_rows"], 1, projection["rows"], "plan.projection.object_rows")
    _integer(projection["eligible_rows"], 1, projection["rows"], "plan.projection.eligible_rows")
    if census is not None:
        for key, name in (("support_sha256", "support.tsv"), ("census_sha256", "rows.tsv")):
            if hashlib.sha256((Path(census) / name).read_bytes()).hexdigest() != projection[key]:
                _fail(f"plan.projection.{key} is not the census {name} digest")
    candidate = _keys(plan["candidate"], ("commit", "tree"), "plan.candidate")
    for key in ("commit", "tree"):
        if type(candidate[key]) is not str or not REVISION_RE.fullmatch(candidate[key]):
            _fail(f"plan.candidate.{key} must be a full 40- or 64-hex revision")
    checks = plan["checks"]
    if type(checks) is not list or not len(KINDS) <= len(checks) <= CHECKS_CAP:
        _fail(f"plan.checks must hold {len(KINDS)}..{CHECKS_CAP} checks")
    pinned = tools(checks_directory, plan["tools"])
    hosted_targets = [check.get("target") for check in checks if type(check) is dict and
                      check.get("evidence") == "hosted"]
    hosted_sha256 = hosted_record(hosted_data, candidate, hosted_targets)
    lines = [HEADER, f"support={projection['support_sha256']}", f"census={projection['census_sha256']}",
             f"population={projection['population_sha256']}", f"native-target={projection['native_target']}",
             f"hosted={candidate['commit']} {candidate['tree']} {hosted_sha256}", f"tools={len(pinned)}",
             *(f"tool={index} {digest} {name}" for index, (name, digest) in enumerate(pinned)),
             f"checks={len(checks)}"]
    identities = []
    for index, check in enumerate(checks):
        block, identity, rows = check_block(index, check, projection, len(pinned), hosted_sha256)
        lines += block
        identities.append((identity, rows))
    coverage(identities, projection)
    data = ("\n".join(lines) + "\n").encode("ascii")
    if len(data) > AUTHORITY_BYTES_CAP:
        _fail(f"the authority exceeds {AUTHORITY_BYTES_CAP} bytes")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plan", type=Path, required=True, help="the reviewed check plan (JSON)")
    parser.add_argument("--checks-dir", type=Path, required=True, help="the directory of pinned ELF check tools")
    parser.add_argument("--hosted-record", type=Path, required=True,
                        help="the BQ-RETIREMENT-HOSTED-ACCEPTANCE-V1 record to bind")
    parser.add_argument("--census", type=Path, help="the installed census whose support and rows pins to verify")
    parser.add_argument("--output", type=Path, required=True, help="the authority to create (never replaced)")
    arguments = parser.parse_args()
    status = 0
    try:
        try:
            plan = json.loads(arguments.plan.read_text(encoding="utf-8"), object_pairs_hook=_json_object)
        except ValueError as error:
            _fail(f"plan is not strict JSON: {error}")
        data = authority(plan, arguments.checks_dir, arguments.hosted_record.read_bytes(), arguments.census)
        with open(arguments.output, "xb") as stream:
            stream.write(data)
    except (ChecksError, OSError) as error:
        print(f"retirement_required_checks: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
