#!/usr/bin/env python3
"""Strict parsing and declaration editing for native-retirement rebinding."""

import ast
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat

import native_retirement_materializer as materializer


REPORT_SCHEMA = "buster-native-retirement-rebind-report-v1"
DESCRIPTOR_PATH = "docs/native-retirement-dependencies-v1.json"
DOCUMENT_PATH = "docs/native-retirement-census.md"
C_BINDING_PATH = "tools/native_retirement_census.c"
PYTHON_BINDING_PATH = "tools/native_retirement_contract.py"
SDK_MANIFEST_PATH = "docs/native-retirement-sdks-v1.json"
TARGET_PATHS = (DESCRIPTOR_PATH, DOCUMENT_PATH, C_BINDING_PATH, PYTHON_BINDING_PATH)
IDENTITY_ORDER = ("descriptor_sha256", "receipt_sha256", "project_sha256", "ledger_sha256")
TOP_LEVEL_ORDER = (
    "schema",
    "version",
    "source_root",
    "archived_replay",
    "metadata",
    "external_checkouts",
    "external_generated",
    "projects",
    "resources",
)
REQUIRED_TOP_LEVEL = frozenset({
    "schema", "version", "source_root", "metadata",
    "external_checkouts", "external_generated", "projects",
})
RECORD_KEYS = ("source", "provenance", "destination", "bytes", "sha256")
SHA256_RE = r"[0-9a-f]{64}"

C_DECLARATIONS = {
    "descriptor_sha256": re.compile(
        rf'^BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_descriptor_sha256 = '
        rf'S8_INITIALIZER\("({SHA256_RE})"\);$', re.MULTILINE),
    "receipt_sha256": re.compile(
        rf'^BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_receipt_sha256 = '
        rf'S8_INITIALIZER\("({SHA256_RE})"\);$', re.MULTILINE),
    "project_sha256": re.compile(
        rf'^BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_project_sha256 = '
        rf'S8_INITIALIZER\("({SHA256_RE})"\);$', re.MULTILINE),
    "ledger_sha256": re.compile(
        rf'^BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_ledger_sha256 = '
        rf'S8_INITIALIZER\("({SHA256_RE})"\);$', re.MULTILINE),
}
PYTHON_DECLARATIONS = {
    "descriptor_sha256": re.compile(
        rf'^FULL_DEPENDENCY_DESCRIPTOR_SHA256 = "({SHA256_RE})"$', re.MULTILINE),
    "receipt_sha256": re.compile(
        rf'^FULL_DEPENDENCY_RECEIPT_SHA256 = "({SHA256_RE})"$', re.MULTILINE),
    "project_sha256": re.compile(
        rf'^FULL_DEPENDENCY_PROJECT_SHA256 = "({SHA256_RE})"$', re.MULTILINE),
    "ledger_sha256": re.compile(
        rf'^FULL_DEPENDENCY_LEDGER_SHA256 = "({SHA256_RE})"$', re.MULTILINE),
}
DOCUMENT_DECLARATION = re.compile(
    rf"(The binding values are frozen in both the C producer and the independent\n"
    rf"validator: descriptor SHA-256\n`)({SHA256_RE})(`, materializer\n"
    rf"receipt SHA-256\n`)({SHA256_RE})(`, project\n"
    rf"closure SHA-256\n`)({SHA256_RE})(`, and\n"
    rf"materializer ledger SHA-256\n`)({SHA256_RE})(`\.)"
)


class RebindError(ValueError):
    """The repository cannot be rebound without weakening its contract."""


@dataclass
class RebindPlan:
    root: Path
    originals: dict
    replacements: dict
    source_bindings: tuple
    report: dict


def _fail(message):
    raise RebindError(message)


def _identity_tuple(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink,
            info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def _absolute_root(value):
    root = Path(os.path.abspath(os.fspath(value)))
    try:
        info = root.lstat()
    except OSError as error:
        _fail(f"cannot inspect repository root {root}: {error}")
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        _fail(f"repository root must be a real directory: {root}")
    return root


def _read_no_follow(path, field, require_single_link=False):
    path = Path(os.path.abspath(os.fspath(path)))
    current = Path(path.anchor)
    descriptor = None
    try:
        parts = path.relative_to(current).parts
        for part in parts[:-1]:
            current /= part
            info = current.lstat()
            if stat.S_ISLNK(info.st_mode):
                _fail(f"{field} contains a symlink: {current}")
            if not stat.S_ISDIR(info.st_mode):
                _fail(f"{field} parent is not a directory: {current}")
        current /= parts[-1]
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            _fail(f"{field} is not a real file: {path}")
        if require_single_link and info.st_nlink != 1:
            _fail(f"{field} is hard-linked: {path}")
        descriptor = os.open(os.fspath(current), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        before = os.fstat(descriptor)
        chunks = bytearray()
        while True:
            block = os.read(descriptor, 1024 * 1024)
            if not block:
                break
            chunks.extend(block)
        after = os.fstat(descriptor)
        if _identity_tuple(before) != _identity_tuple(after):
            _fail(f"{field} changed while reading: {path}")
        return bytes(chunks)
    except FileNotFoundError:
        _fail(f"missing {field}: {path}")
    except OSError as error:
        _fail(f"cannot read {field} {path}: {error}")
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _decode(data, field):
    try:
        return data.decode("utf-8")
    except UnicodeError as error:
        _fail(f"{field} is not UTF-8: {error}")


def _json_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            _fail(f"dependency descriptor repeats JSON key {key!r}")
        value[key] = item
    return value


def _canonical_json(value):
    return (json.dumps(value, ensure_ascii=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _load_descriptor(raw):
    try:
        value = json.loads(_decode(raw, "dependency descriptor"), object_pairs_hook=_json_object)
    except json.JSONDecodeError as error:
        _fail(f"cannot parse dependency descriptor: {error}")
    if not isinstance(value, dict):
        _fail("dependency descriptor must be a JSON object")
    if _canonical_json(value) != raw:
        _fail("dependency descriptor is not canonical two-space JSON with one trailing newline")
    keys = tuple(value)
    unknown = set(keys) - set(TOP_LEVEL_ORDER)
    missing = REQUIRED_TOP_LEVEL - set(keys)
    if unknown:
        _fail(f"dependency descriptor has unexpected fields: {', '.join(sorted(unknown))}")
    if missing:
        _fail(f"dependency descriptor is missing fields: {', '.join(sorted(missing))}")
    expected_order = tuple(key for key in TOP_LEVEL_ORDER if key in value)
    if keys != expected_order:
        _fail("dependency descriptor top-level fields are not in the reviewed order")
    if value.get("schema") != materializer.SCHEMA or value.get("version") != materializer.VERSION:
        _fail("dependency descriptor schema/version is not the materializer contract")
    if value.get("source_root") != "..":
        _fail("dependency descriptor source_root must be exactly '..'")
    if not isinstance(value.get("metadata"), list):
        _fail("dependency descriptor metadata must be a list")
    if not isinstance(value.get("external_checkouts"), list) or not isinstance(value.get("external_generated"), list):
        _fail("dependency descriptor external declarations must be lists")
    for collection in ("projects", "resources"):
        if collection == "resources" and collection not in value:
            continue
        records = value.get(collection)
        if not isinstance(records, list) or not records:
            _fail(f"dependency descriptor {collection} must be a non-empty list")
        for index, record in enumerate(records):
            if not isinstance(record, dict) or tuple(record) != RECORD_KEYS:
                _fail(f"dependency descriptor {collection}[{index}] has an unexpected declaration")
    materializer.parse_manifest(value)
    return value


def _eligible_projects(manifest):
    eligible = []
    seen = set()
    for index, record in enumerate(manifest["projects"]):
        source = record["source"]
        provenance = record["provenance"]
        if not isinstance(source, str) or not isinstance(provenance, str):
            _fail(f"dependency descriptor projects[{index}] source/provenance must be text")
        if provenance.startswith("repo:"):
            if provenance != f"repo:{source}":
                _fail(f"repository project provenance does not exactly name its source: {source}")
            if source in seen:
                _fail(f"repository project source is duplicated: {source}")
            seen.add(source)
            eligible.append((index, record))
    if not eligible:
        _fail("dependency descriptor has no repository-owned project records")
    return eligible


def _literal_assignment(text, name):
    try:
        tree = ast.parse(text, filename=PYTHON_BINDING_PATH)
    except SyntaxError as error:
        _fail(f"cannot parse {PYTHON_BINDING_PATH}: {error}")
    matches = []
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            matches.append(node.value)
    if len(matches) != 1:
        _fail(f"expected exactly one reviewed Python {name} declaration, found {len(matches)}")
    try:
        return ast.literal_eval(matches[0])
    except (ValueError, TypeError) as error:
        _fail(f"Python {name} is not a literal declaration: {error}")


def _validate_external_declarations(manifest, contract_text):
    expected_checkouts = _literal_assignment(contract_text, "FULL_EXTERNAL_CHECKOUTS")
    expected_generated = _literal_assignment(contract_text, "FULL_EXTERNAL_GENERATED")
    if not isinstance(expected_checkouts, tuple) or not isinstance(expected_generated, tuple):
        _fail("independent external dependency declarations must be tuples")
    if manifest["external_checkouts"] != list(expected_checkouts):
        _fail("dependency descriptor external checkout pins differ from the independent contract")
    if manifest["external_generated"] != list(expected_generated):
        _fail("dependency descriptor generated-external pins differ from the independent contract")


def _load_sdk_manifest(raw):
    try:
        value = json.loads(_decode(raw, "SDK manifest"), object_pairs_hook=_json_object)
    except json.JSONDecodeError as error:
        _fail(f"cannot parse SDK manifest: {error}")
    if not isinstance(value, dict) or tuple(value) != ("version", "archives", "files"):
        _fail("SDK manifest has an unexpected declaration")
    if value.get("version") != 1:
        _fail("SDK manifest version is unsupported")
    archives = value.get("archives")
    files = value.get("files")
    if not isinstance(archives, list) or not isinstance(files, list):
        _fail("SDK manifest archives and files must be lists")
    by_name = {}
    for index, archive in enumerate(archives):
        if not isinstance(archive, dict) or tuple(archive) != ("name", "url", "sha256", "bytes"):
            _fail(f"SDK manifest archive {index} has an unexpected declaration")
        name = archive["name"]
        if not isinstance(name, str) or not name or name in by_name:
            _fail(f"SDK manifest archive {index} has an invalid or duplicate name")
        if not isinstance(archive["sha256"], str) or not re.fullmatch(SHA256_RE, archive["sha256"]):
            _fail(f"SDK manifest archive {name} has an invalid SHA-256")
        if type(archive["bytes"]) is not int or archive["bytes"] < 0:
            _fail(f"SDK manifest archive {name} has an invalid byte count")
        by_name[name] = archive
    seen_sources = set()
    for index, record in enumerate(files):
        if not isinstance(record, dict) or tuple(record) != ("archive", "member", "source", "bytes", "sha256"):
            _fail(f"SDK manifest file {index} has an unexpected declaration")
        source = record["source"]
        if (not isinstance(source, str) or not source.startswith("sdk-headers/") or
                source in seen_sources):
            _fail(f"SDK manifest file {index} has an invalid or duplicate source")
        if record["archive"] not in by_name:
            _fail(f"SDK manifest file {source} names an unknown archive")
        if not isinstance(record["member"], str) or not record["member"]:
            _fail(f"SDK manifest file {source} has no archive member")
        if type(record["bytes"]) is not int or record["bytes"] < 0:
            _fail(f"SDK manifest file {source} has an invalid byte count")
        if not isinstance(record["sha256"], str) or not re.fullmatch(SHA256_RE, record["sha256"]):
            _fail(f"SDK manifest file {source} has an invalid SHA-256")
        seen_sources.add(source)
    return value, by_name


def _validate_sdk_declarations(manifest, sdk_manifest_raw):
    sdk_manifest, archives = _load_sdk_manifest(sdk_manifest_raw)
    project_records = {record["source"]: record for record in manifest["projects"]}
    expected_sources = set()
    for record in sdk_manifest["files"]:
        source = record["source"]
        expected_sources.add(source)
        dependency = project_records.get(source)
        if dependency is None:
            _fail(f"SDK manifest source is absent from the dependency descriptor: {source}")
        archive = archives[record["archive"]]
        expected_provenance = f"sdk/{archive['sha256']}/{record['member']}"
        if (dependency["provenance"] != expected_provenance or
                dependency["bytes"] != record["bytes"] or
                dependency["sha256"] != record["sha256"]):
            _fail(f"SDK dependency binding mismatch: {source}")
    actual_sources = {
        record["source"] for record in manifest["projects"]
        if record["source"].startswith("sdk-headers/")
    }
    if actual_sources != expected_sources:
        _fail("SDK inventory does not match the dependency descriptor")


def _source_identity(root, source, field="authenticated dependency source"):
    pure = PurePosixPath(source)
    if pure.is_absolute() or not pure.parts or any(part in ("", ".", "..") for part in pure.parts):
        _fail(f"{field} is not canonical: {source!r}")
    path = root.joinpath(*pure.parts)
    data = _read_no_follow(path, f"{field} {source}", require_single_link=True)
    return len(data), hashlib.sha256(data).hexdigest()


def _single_match(pattern, text, field):
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        _fail(f"expected exactly one reviewed {field} declaration, found {len(matches)}")
    return matches[0]


def _extract_bindings(originals):
    c_text = _decode(originals[C_BINDING_PATH], C_BINDING_PATH)
    python_text = _decode(originals[PYTHON_BINDING_PATH], PYTHON_BINDING_PATH)
    document_text = _decode(originals[DOCUMENT_PATH], DOCUMENT_PATH)
    by_file = {C_BINDING_PATH: {}, PYTHON_BINDING_PATH: {}, DOCUMENT_PATH: {}}
    for name, pattern in C_DECLARATIONS.items():
        by_file[C_BINDING_PATH][name] = _single_match(pattern, c_text, f"C {name}").group(1)
    for name, pattern in PYTHON_DECLARATIONS.items():
        by_file[PYTHON_BINDING_PATH][name] = _single_match(pattern, python_text, f"Python {name}").group(1)
    match = _single_match(DOCUMENT_DECLARATION, document_text, "documentation dependency binding")
    for name, group in zip(IDENTITY_ORDER, (2, 4, 6, 8)):
        by_file[DOCUMENT_PATH][name] = match.group(group)
    result = {}
    for name in IDENTITY_ORDER:
        values = {path: by_file[path][name] for path in by_file}
        distinct = set(values.values())
        if len(distinct) != 1:
            rendered = ", ".join(f"{path}={value}" for path, value in sorted(values.items()))
            _fail(f"inconsistent {name} declarations: {rendered}")
        result[name] = distinct.pop()
    return result


def _replace_pattern(text, pattern, value, field):
    match = _single_match(pattern, text, field)
    return text[:match.start(1)] + value + text[match.end(1):]


def _replace_bindings(originals, identities):
    c_text = _decode(originals[C_BINDING_PATH], C_BINDING_PATH)
    python_text = _decode(originals[PYTHON_BINDING_PATH], PYTHON_BINDING_PATH)
    for name in IDENTITY_ORDER:
        c_text = _replace_pattern(c_text, C_DECLARATIONS[name], identities[name], f"C {name}")
        python_text = _replace_pattern(
            python_text, PYTHON_DECLARATIONS[name], identities[name], f"Python {name}")
    document_text = _decode(originals[DOCUMENT_PATH], DOCUMENT_PATH)
    match = _single_match(DOCUMENT_DECLARATION, document_text, "documentation dependency binding")
    replacement = "".join((
        match.group(1), identities["descriptor_sha256"], match.group(3), identities["receipt_sha256"],
        match.group(5), identities["project_sha256"], match.group(7), identities["ledger_sha256"],
        match.group(9),
    ))
    document_text = document_text[:match.start()] + replacement + document_text[match.end():]
    return {
        DOCUMENT_PATH: document_text.encode("utf-8"),
        C_BINDING_PATH: c_text.encode("utf-8"),
        PYTHON_BINDING_PATH: python_text.encode("utf-8"),
    }
