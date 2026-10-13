#!/usr/bin/env python3
"""Validate and render the evidence-backed backend capability catalogue.

Usage:
    python3 tools/backend_capabilities.py --check
    python3 tools/backend_capabilities.py --write
    python3 tools/backend_capabilities.py --json
    python3 tools/backend_capabilities.py --ci-report LOG
"""

import argparse
import collections
import json
import os
import posixpath
import re
import subprocess
import sys
from pathlib import Path

SCHEMA = "buster-backend-capabilities-v1"
CATALOGUE = "docs/backend-capabilities-v1.json"
OUTPUT = "docs/backend-capabilities.md"
STATUSES = ["supported", "partial", "unsupported-diagnostic", "not-applicable", "unknown"]
BRACE = re.compile(r"[{}]")
WORD = re.compile(r"\w+")
NOT_NEWLINE = re.compile(r"[^\n]")
C_NONCODE = re.compile(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", re.S)
UNIT_TEST_FUNCTION = re.compile(r"\bUnitTestResult\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", re.S)
MODULE_TABLE = re.compile(r"\btest_descriptors\s*\[[^\]]*\]\s*=\s*\{")
MODULE_ENTRY = re.compile(
    r'\[\s*[A-Za-z_]\w*\s*\]\s*=\s*\{\s*S8_INITIALIZER\("([^"]+)"\)\s*,\s*&\s*([A-Za-z_]\w*)'
)
TIMING_LINE = re.compile(
    r"^TEST_MODULE_TIMING index=(\d+) module=([A-Za-z_]\w*) duration_ns=(\d+) "
    r"passed=(\d+) failed=(\d+) assertions=(\d+) status=(pass|fail)$"
)
FIXTURE_START_LINE = re.compile(
    r"^TEST_FIXTURE_START_V1 kind=(module|fixture) module=([A-Za-z_]\w*) "
    r"fixture=([A-Za-z_]\w*) index=(\d+)$"
)
TOP_LEVEL_KEYS = {
    "schema",
    "reviewed_revision",
    "owner_issue",
    "levels",
    "statuses",
    "backends",
    "features",
    "requirements",
    "cells",
}
OBJECT_KEYS = {
    "level": ({"id", "name"}, {"id", "name"}),
    "backend": ({"id", "name", "kind", "doc"}, {"id", "name", "kind", "doc"}),
    "feature": ({"id", "name"}, {"id", "name"}),
    "cell": (
        {"backend", "feature", "status"},
        {"backend", "feature", "status", "evidence", "diagnostics", "note"},
    ),
    "evidence": (
        {"file", "provides"},
        {"file", "symbol", "anchor", "provides", "requires"},
    ),
    "diagnostic": ({"file", "text"}, {"file", "text"}),
    "requirement": ({"text", "missing"}, {"text", "missing"}),
}


def catalogue_error(label, location, message):
    return f"{label}: {location}: {message}"


def check_object(value, object_type, location, errors, label):
    required, allowed = OBJECT_KEYS[object_type]
    if not isinstance(value, dict):
        errors.append(catalogue_error(label, location, "expected an object"))
        return False
    for key in sorted(required - value.keys()):
        errors.append(catalogue_error(label, location, f"missing required key {key!r}"))
    for key in sorted(value.keys() - allowed):
        errors.append(catalogue_error(label, location, f"unknown key {key!r}"))
    return True


def nonempty_string(value):
    return isinstance(value, str) and bool(value.strip())


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def source_files(root):
    directory = root / "src" / "buster" / "tests"
    files = {}
    if directory.is_dir():
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                text = read_text(path)
                if text is not None:
                    files[path] = text
    return files


def mask_noncode(text):
    return C_NONCODE.sub(lambda match: NOT_NEWLINE.sub(" ", match.group()), text)


def closing_brace(text, opening):
    depth = 0
    for brace in BRACE.finditer(text, opening):
        depth += 1 if brace.group() == "{" else -1
        if depth == 0:
            return brace.start()
    return None


def registered_test_modules(root):
    functions = {}
    errors = []
    for path, text in source_files(root).items():
        if path.suffix not in (".c", ".h"):
            continue
        code = mask_noncode(text)
        for match in UNIT_TEST_FUNCTION.finditer(code):
            end = closing_brace(code, match.end() - 1)
            if end is None:
                errors.append(f"{path}: unterminated UnitTestResult function {match.group(1)!r}")
                continue
            name = match.group(1)
            body = code[match.end():end]
            functions.setdefault(name, set()).update(re.findall(r"\b[A-Za-z_]\w*\b", body))

    manifest = root / "src" / "buster" / "tests" / "test.c"
    manifest_text = read_text(manifest)
    if manifest_text is None:
        return {}, [f"{manifest}: could not read test module manifest"]
    manifest_code = mask_noncode(manifest_text)
    table = MODULE_TABLE.search(manifest_code)
    if table is None:
        return {}, [f"{manifest}: test_descriptors table not found"]
    end = closing_brace(manifest_code, table.end() - 1)
    if end is None:
        return {}, [f"{manifest}: unterminated test_descriptors table"]
    entries = MODULE_ENTRY.findall(manifest_text[table.end():end])
    if not entries:
        return {}, [f"{manifest}: test_descriptors has no module entries"]

    modules = {}
    for module, entry in entries:
        if entry not in functions:
            errors.append(f"{manifest}: module {module!r} references undefined UnitTestResult {entry!r}")
            continue
        visited = set()
        pending = [entry]
        while pending:
            function = pending.pop()
            if function in visited:
                continue
            visited.add(function)
            reached = modules.setdefault(function, [])
            if module not in reached:
                reached.append(module)
            pending.extend(functions.get(function, set()) - visited)
    return modules, errors


def test_command(modules):
    return f"build/Release/ide test --module={','.join(modules)} --ci=1"


def rooted_file(root, value):
    path = Path(value)
    return path if path.is_absolute() else root / path


def validate_catalogue(data, root, label=CATALOGUE):
    errors = []
    if not isinstance(data, dict):
        return [catalogue_error(label, "$", "expected a top-level object")]

    for key in sorted(TOP_LEVEL_KEYS - data.keys()):
        errors.append(catalogue_error(label, "$", f"missing required key {key!r}"))
    for key in sorted(data.keys() - TOP_LEVEL_KEYS):
        errors.append(catalogue_error(label, "$", f"unknown key {key!r}"))

    if data.get("schema") != SCHEMA:
        errors.append(catalogue_error(label, "schema", f"must equal {SCHEMA!r}"))
    revision = data.get("reviewed_revision")
    if not isinstance(revision, str) or re.fullmatch(r"[0-9a-fA-F]{40}", revision) is None:
        errors.append(catalogue_error(label, "reviewed_revision", "must be 40 hexadecimal characters"))
    owner_issue = data.get("owner_issue")
    if not isinstance(owner_issue, int) or isinstance(owner_issue, bool):
        errors.append(catalogue_error(label, "owner_issue", "must be an integer"))

    if data.get("statuses") != STATUSES:
        errors.append(catalogue_error(label, "statuses", f"must equal {json.dumps(STATUSES)}"))

    requirements = data.get("requirements")
    if not isinstance(requirements, dict):
        errors.append(catalogue_error(label, "requirements", "expected an object"))
        requirements = {}
    else:
        for key, value in requirements.items():
            if not nonempty_string(key):
                errors.append(catalogue_error(label, "requirements", "keys must be non-empty strings"))
            location = f"requirements.{key}"
            if check_object(value, "requirement", location, errors, label):
                if not nonempty_string(value.get("text")):
                    errors.append(catalogue_error(label, f"{location}.text", "must be a non-empty string"))
                if value.get("missing") not in ("skip", "fail"):
                    errors.append(catalogue_error(label, f"{location}.missing", "must be 'skip' or 'fail'"))

    named_values = {}
    for collection, object_type in (
        ("levels", "level"),
        ("backends", "backend"),
        ("features", "feature"),
    ):
        values = data.get(collection)
        if not isinstance(values, list):
            errors.append(catalogue_error(label, collection, "expected an array"))
            named_values[collection] = []
            continue
        seen = set()
        valid_values = []
        for index, value in enumerate(values):
            location = f"{collection}[{index}]"
            if not check_object(value, object_type, location, errors, label):
                continue
            identifier = value.get("id")
            name = value.get("name")
            if not nonempty_string(identifier):
                errors.append(catalogue_error(label, f"{location}.id", "must be a non-empty string"))
            elif identifier in seen:
                errors.append(catalogue_error(label, f"{location}.id", f"duplicate id {identifier!r}"))
            else:
                seen.add(identifier)
            if not nonempty_string(name):
                errors.append(catalogue_error(label, f"{location}.name", "must be a non-empty string"))
            valid_values.append(value)
            if object_type == "backend":
                if value.get("kind") not in ("direct-codegen", "external-tool"):
                    errors.append(catalogue_error(
                        label, f"{location}.kind", "must be 'direct-codegen' or 'external-tool'"))
                doc = value.get("doc")
                if not nonempty_string(doc):
                    errors.append(catalogue_error(label, f"{location}.doc", "must be a non-empty file path"))
                elif not rooted_file(root, doc).is_file():
                    errors.append(catalogue_error(label, f"{location}.doc", f"file does not exist: {doc}"))
        named_values[collection] = valid_values

    level_ids = {
        value["id"] for value in named_values.get("levels", [])
        if nonempty_string(value.get("id"))
    }
    backend_ids = {
        value["id"] for value in named_values.get("backends", [])
        if nonempty_string(value.get("id"))
    }
    feature_ids = {
        value["id"] for value in named_values.get("features", [])
        if nonempty_string(value.get("id"))
    }
    test_texts = source_files(root)
    # A word's whole-word matches are exactly its maximal \w runs: count them
    # once instead of rescanning every test source for each evidence symbol.
    test_words = collections.Counter(word for text in test_texts.values() for word in WORD.findall(text))
    modules_by_symbol, module_errors = registered_test_modules(root)
    errors.extend(catalogue_error(label, "test modules", error) for error in module_errors)

    cells = data.get("cells")
    if not isinstance(cells, list):
        errors.append(catalogue_error(label, "cells", "expected an array"))
        cells = []
    seen_cells = set()
    for index, cell in enumerate(cells):
        location = f"cells[{index}]"
        if not check_object(cell, "cell", location, errors, label):
            continue
        backend = cell.get("backend")
        feature = cell.get("feature")
        status = cell.get("status")
        for field, value in (("backend", backend), ("feature", feature), ("status", status)):
            if not nonempty_string(value):
                errors.append(catalogue_error(label, f"{location}.{field}", "must be a non-empty string"))
        if not isinstance(backend, str) or backend not in backend_ids:
            errors.append(catalogue_error(label, f"{location}.backend", f"unknown backend {backend!r}"))
        if not isinstance(feature, str) or feature not in feature_ids:
            errors.append(catalogue_error(label, f"{location}.feature", f"unknown feature {feature!r}"))
        if status not in STATUSES:
            errors.append(catalogue_error(label, f"{location}.status", f"unknown status {status!r}"))
        if isinstance(backend, str) and isinstance(feature, str):
            pair = (backend, feature)
            if pair in seen_cells:
                errors.append(catalogue_error(label, location, f"duplicate backend/feature cell {pair!r}"))
            else:
                seen_cells.add(pair)

        note = cell.get("note")
        if "note" in cell and not nonempty_string(note):
            errors.append(catalogue_error(label, f"{location}.note", "must be a non-empty string"))

        evidence = cell.get("evidence", [])
        if not isinstance(evidence, list):
            errors.append(catalogue_error(label, f"{location}.evidence", "expected an array"))
            evidence = []
        for evidence_index, entry in enumerate(evidence):
            entry_location = f"{location}.evidence[{evidence_index}]"
            if not check_object(entry, "evidence", entry_location, errors, label):
                continue
            file_name = entry.get("file")
            file_text = None
            if not nonempty_string(file_name):
                errors.append(catalogue_error(label, f"{entry_location}.file", "must be a non-empty file path"))
            else:
                file_path = rooted_file(root, file_name)
                if not file_path.is_file():
                    errors.append(catalogue_error(
                        label, f"{entry_location}.file", f"file does not exist: {file_name}"))
                else:
                    file_text = read_text(file_path)
                    if file_text is None:
                        errors.append(catalogue_error(
                            label, f"{entry_location}.file", f"could not read file: {file_name}"))

            has_symbol = "symbol" in entry
            has_anchor = "anchor" in entry
            if has_symbol == has_anchor:
                errors.append(catalogue_error(
                    label, entry_location, "must contain exactly one of 'symbol' or 'anchor'"))
            if has_symbol:
                symbol = entry.get("symbol")
                if not nonempty_string(symbol):
                    errors.append(catalogue_error(label, f"{entry_location}.symbol", "must be a non-empty string"))
                elif file_text is not None:
                    definition = re.compile(rf"UnitTestResult\s+{re.escape(symbol)}\s*\(")
                    if definition.search(file_text) is None:
                        errors.append(catalogue_error(
                            label, f"{entry_location}.symbol",
                            f"no UnitTestResult definition for {symbol!r} in {file_name}"))
                    if WORD.fullmatch(symbol):
                        count = test_words[symbol]
                    else:
                        occurrence = re.compile(rf"\b{re.escape(symbol)}\b")
                        count = sum(len(occurrence.findall(text)) for text in test_texts.values())
                    if count < 2:
                        errors.append(catalogue_error(
                            label, f"{entry_location}.symbol",
                            f"{symbol!r} occurs {count} times under src/buster/tests; expected at least 2"))
                    if not modules_by_symbol.get(symbol):
                        errors.append(catalogue_error(
                            label, f"{entry_location}.symbol",
                            f"{symbol!r} is not reachable from any registered test module"))
            if has_anchor:
                anchor = entry.get("anchor")
                if not nonempty_string(anchor):
                    errors.append(catalogue_error(label, f"{entry_location}.anchor", "must be a non-empty string"))
                elif file_text is not None and anchor not in file_text:
                    errors.append(catalogue_error(
                        label, f"{entry_location}.anchor", f"literal anchor not found in {file_name}: {anchor!r}"))

            provides = entry.get("provides")
            if not isinstance(provides, list) or not provides:
                errors.append(catalogue_error(
                    label, f"{entry_location}.provides", "must be a non-empty array of level ids"))
            else:
                if len(set(item for item in provides if isinstance(item, str))) != len(provides):
                    errors.append(catalogue_error(label, f"{entry_location}.provides", "must not contain duplicates"))
                for provided in provides:
                    if not isinstance(provided, str) or provided not in level_ids:
                        errors.append(catalogue_error(
                            label, f"{entry_location}.provides", f"unknown level id {provided!r}"))
            if "requires" in entry and (
                    not isinstance(entry["requires"], str) or entry["requires"] not in requirements):
                errors.append(catalogue_error(
                    label, f"{entry_location}.requires", f"unknown requirement {entry['requires']!r}"))

        diagnostics = cell.get("diagnostics", [])
        if not isinstance(diagnostics, list):
            errors.append(catalogue_error(label, f"{location}.diagnostics", "expected an array"))
            diagnostics = []
        for diagnostic_index, diagnostic in enumerate(diagnostics):
            diagnostic_location = f"{location}.diagnostics[{diagnostic_index}]"
            if not check_object(diagnostic, "diagnostic", diagnostic_location, errors, label):
                continue
            file_name = diagnostic.get("file")
            text = diagnostic.get("text")
            if not nonempty_string(text):
                errors.append(catalogue_error(
                    label, f"{diagnostic_location}.text", "must be a non-empty string"))
            if not nonempty_string(file_name):
                errors.append(catalogue_error(
                    label, f"{diagnostic_location}.file", "must be a non-empty file path"))
            elif not rooted_file(root, file_name).is_file():
                errors.append(catalogue_error(
                    label, f"{diagnostic_location}.file", f"file does not exist: {file_name}"))
            else:
                file_text = read_text(rooted_file(root, file_name))
                if file_text is None:
                    errors.append(catalogue_error(
                        label, f"{diagnostic_location}.file", f"could not read file: {file_name}"))
                elif nonempty_string(text) and text not in file_text:
                    errors.append(catalogue_error(
                        label, f"{diagnostic_location}.text", f"literal diagnostic text not found in {file_name}"))
            if nonempty_string(text) and not any(text in test_text for test_text in test_texts.values()):
                errors.append(catalogue_error(
                    label, f"{diagnostic_location}.text",
                    f"diagnostic text is not asserted under src/buster/tests: {text!r}"))

        evidence_count = len(evidence)
        diagnostic_count = len(diagnostics)
        if status == "supported" and evidence_count < 1:
            errors.append(catalogue_error(label, location, "supported cells require at least one evidence entry"))
        elif status == "partial":
            if evidence_count < 1:
                errors.append(catalogue_error(label, location, "partial cells require at least one evidence entry"))
            if not nonempty_string(note):
                errors.append(catalogue_error(label, f"{location}.note", "partial cells require a note"))
        elif status == "unsupported-diagnostic":
            if diagnostic_count < 1:
                errors.append(catalogue_error(
                    label, location, "unsupported-diagnostic cells require at least one diagnostic"))
            if evidence_count:
                errors.append(catalogue_error(
                    label, location, "unsupported-diagnostic cells must not contain evidence"))
        elif status == "not-applicable":
            if not nonempty_string(note):
                errors.append(catalogue_error(label, f"{location}.note", "not-applicable cells require a note"))
            if evidence_count:
                errors.append(catalogue_error(label, location, "not-applicable cells must not contain evidence"))
            if diagnostic_count:
                errors.append(catalogue_error(label, location, "not-applicable cells must not contain diagnostics"))
        elif status == "unknown":
            if not nonempty_string(note):
                errors.append(catalogue_error(label, f"{location}.note", "unknown cells require a note"))
            if evidence_count:
                errors.append(catalogue_error(label, location, "unknown cells must not contain evidence"))

    expected_cells = {(backend, feature) for backend in backend_ids for feature in feature_ids}
    for backend, feature in sorted(expected_cells - seen_cells):
        errors.append(catalogue_error(label, "cells", f"missing backend/feature cell {(backend, feature)!r}"))
    return errors


def diagnostic_is_tested(root, text, test_texts=None):
    if test_texts is None:
        test_texts = source_files(root)
    return any(text in content for content in test_texts.values())


def derived_report(data, root):
    level_ids = [item["id"] for item in data["levels"]]
    cells = []
    summary = {
        backend["id"]: {status: 0 for status in STATUSES}
        for backend in data["backends"]
    }
    test_texts = source_files(root)
    modules_by_symbol, _ = registered_test_modules(root)
    for cell in data["cells"]:
        levels = {
            level
            for evidence in cell.get("evidence", [])
            for level in evidence["provides"]
        }
        diagnostics = [
            {
                "file": diagnostic["file"],
                "text": diagnostic["text"],
                "diagnostic_tested": diagnostic_is_tested(root, diagnostic["text"], test_texts),
            }
            for diagnostic in cell.get("diagnostics", [])
        ]
        evidence = []
        for entry in cell.get("evidence", []):
            detail = dict(entry)
            modules = modules_by_symbol.get(entry.get("symbol"), []) if "symbol" in entry else []
            detail["modules"] = modules
            if modules:
                detail["test_command"] = test_command(modules)
            evidence.append(detail)
        report_cell = {
            "backend": cell["backend"],
            "feature": cell["feature"],
            "status": cell["status"],
            "levels": [level for level in level_ids if level in levels],
            "evidence": evidence,
            "diagnostics": diagnostics,
        }
        cells.append(report_cell)
        summary[cell["backend"]][cell["status"]] += 1
    return {"cells": cells, "summary": summary, "requirements": data["requirements"]}


def code_span(text):
    delimiter = "`" * (max((len(match.group(0)) for match in re.finditer(r"`+", text)), default=0) + 1)
    padding = " " if text.startswith("`") or text.endswith("`") else ""
    return f"{delimiter}{padding}{text}{padding}{delimiter}"


def markdown_cell(text):
    return str(text).replace("|", r"\|")


def render_catalogue(data, root):
    report = derived_report(data, root)
    cell_by_pair = {(cell["backend"], cell["feature"]): cell for cell in data["cells"]}
    report_by_pair = {(cell["backend"], cell["feature"]): cell for cell in report["cells"]}
    test_texts = source_files(root)
    level_names = {level["id"]: level["name"] for level in data["levels"]}
    requirements = data["requirements"]
    lines = [
        "# Backend capabilities",
        "",
        "Generated from [backend-capabilities-v1.json](backend-capabilities-v1.json) by "
        "`python3 tools/backend_capabilities.py --write` — do not edit by hand; "
        "owned by [#309](https://github.com/buster14a/buster/issues/309).",
        "",
        f"Reviewed revision `{data['reviewed_revision']}`.",
        "",
        "## Reading this table",
        "",
        "`supported` has positive evidence, `partial` has bounded evidence and a note, "
        "`unsupported-diagnostic` records an explicit rejection, and `not-applicable` "
        "marks a separate boundary. `unknown` is not a pass. Evidence levels: "
        + "; ".join(f"{code_span(identifier)} → {name}" for identifier, name in level_names.items())
        + ".",
        "",
    ]
    if requirements:
        lines.append(
            "Evidence marked **requires X** depends on tool X. If X is missing, a `skip` "
            "requirement means the evidence is skipped, not passed. A `fail` requirement "
            "records a test failure. Requirements: "
            + "; ".join(
                f"{code_span(key)} (missing: {value['missing']}): {value['text']}"
                for key, value in requirements.items()
            )
            + "."
        )
    else:
        lines.append("No evidence requirements are declared.")
    lines.extend([
        "",
        "A registered target or emitted bytes are not a support claim. Direct code generation "
        "and external GPU orchestration are separate rows.",
        "",
        "## Summary",
        "",
        "| Backend | " + " | ".join(markdown_cell(feature["name"]) for feature in data["features"]) + " |",
        "|---|" + "|".join("---" for _ in data["features"]) + "|",
    ])
    for backend in data["backends"]:
        relative_doc = posixpath.relpath(backend["doc"], "docs")
        backend_label = f"[{markdown_cell(backend['name'])}]({relative_doc})"
        summary_cells = []
        for feature in data["features"]:
            cell = cell_by_pair[(backend["id"], feature["id"])]
            levels = report_by_pair[(backend["id"], feature["id"])]["levels"]
            description = cell["status"]
            if levels:
                description += f" ({', '.join(levels)})"
            summary_cells.append(markdown_cell(description))
        lines.append("| " + " | ".join([backend_label] + summary_cells) + " |")

    for backend in data["backends"]:
        lines.extend([
            "",
            f"## {markdown_cell(backend['name'])}",
            "",
            "| Feature | Status | Levels | Evidence | Note |",
            "|---|---|---|---|---|",
        ])
        for feature in data["features"]:
            cell = cell_by_pair[(backend["id"], feature["id"])]
            levels = [
                level
                for level in [item["id"] for item in data["levels"]]
                if any(level in evidence["provides"] for evidence in cell.get("evidence", []))
            ]
            evidence_text = []
            report_evidence = report_by_pair[(backend["id"], feature["id"])]["evidence"]
            for evidence, derived_evidence in zip(cell.get("evidence", []), report_evidence):
                if "symbol" in evidence:
                    item = f"{code_span(evidence['symbol'])} in {code_span(evidence['file'])}"
                    if derived_evidence["modules"]:
                        item += f"<br>test {code_span(derived_evidence['test_command'])}"
                else:
                    item = f"anchor {code_span(evidence['anchor'])} in {code_span(evidence['file'])}"
                if "requires" in evidence:
                    requirement = requirements[evidence["requires"]]
                    item += (
                        f" (requires {code_span(evidence['requires'])}; "
                        f"missing: {requirement['missing']})"
                    )
                evidence_text.append(item)
            for diagnostic in cell.get("diagnostics", []):
                tested = ("tested" if diagnostic_is_tested(root, diagnostic["text"], test_texts)
                          else "source only")
                evidence_text.append(
                    f"diagnostic {code_span(diagnostic['text'])} ({tested})")
            lines.append("| " + " | ".join([
                markdown_cell(feature["name"]),
                markdown_cell(cell["status"]),
                markdown_cell(", ".join(levels) if levels else "—"),
                markdown_cell("<br>".join(evidence_text) if evidence_text else "—"),
                markdown_cell(cell.get("note", "—")),
            ]) + " |")
    lines.extend([
        "",
        "## Updating this catalogue",
        "",
        "Edit `docs/backend-capabilities-v1.json`, run "
        "`python3 tools/backend_capabilities.py --write`, and commit both files. "
        "CI runs `python3 -B tools/backend_capabilities_test.py` and `--check` "
        "to verify the catalogue and detect drift.",
        "",
    ])
    return "\n".join(lines)


def load_catalogue(path, label):
    try:
        with path.open(encoding="utf-8") as handle:
            return json.load(handle), []
    except OSError as error:
        return None, [catalogue_error(label, "$", f"could not read catalogue: {error}")]
    except json.JSONDecodeError as error:
        return None, [catalogue_error(label, f"line {error.lineno}", f"invalid JSON: {error.msg}")]


def render_ci_report(data, root, log_text, environment=None):
    statuses = {}
    observed = set()
    errors = []
    for line_number, line in enumerate(log_text.splitlines(), 1):
        line = line.strip()
        if line.startswith("TEST_MODULE_TIMING"):
            match = TIMING_LINE.fullmatch(line)
            if match is None:
                errors.append(f"line {line_number}: malformed TEST_MODULE_TIMING record")
                continue
            module, status = match.group(2), "passed" if match.group(7) == "pass" else "failed"
            if status == "failed" or module not in statuses:
                statuses[module] = status
            continue
        if not line.startswith("TEST_FIXTURE_START_V1"):
            continue
        match = FIXTURE_START_LINE.fullmatch(line)
        if match is None:
            errors.append(f"line {line_number}: malformed TEST_FIXTURE_START_V1 record")
        elif match.group(1) == "fixture":
            observed.add((match.group(2), match.group(3)))
    if errors:
        return None, errors

    environment = os.environ if environment is None else environment
    revision = environment.get("GITHUB_SHA")
    if not revision:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=root, check=True, capture_output=True, text=True
        ).stdout.strip()
    run_url = None
    server = environment.get("GITHUB_SERVER_URL")
    repository = environment.get("GITHUB_REPOSITORY")
    run_id = environment.get("GITHUB_RUN_ID")
    if server and repository and run_id:
        run_url = f"{server.rstrip('/')}/{repository.strip('/')}/actions/runs/{run_id}"

    report = derived_report(data, root)
    lines = ["# Backend capability test evidence", "", f"Revision: {code_span(revision)}"]
    if run_url:
        lines.append(f"Run: {code_span(run_url)}")
    lines.extend([
        "",
        "Status legend: `fixture observed; module passed` and `fixture observed; module failed` "
        "mean an exact fixture-start record was found; `module passed; fixture not observed "
        "(may be compiled out on this host)` and `module failed; fixture not observed` mean "
        "the module ran without that record; `not run` means no module result was recorded. "
        "A `missing: skip` requirement is reported as possibly skipped when its module ran.",
        "",
        "| Backend | Feature | Evidence | Module | Status | Note / scope |",
        "|---|---|---|---|---|---|",
    ])
    notes = {
        (cell["backend"], cell["feature"]): cell.get("note", "—")
        for cell in data["cells"]
    }
    for cell in report["cells"]:
        for evidence in cell["evidence"]:
            label = (
                code_span(evidence["symbol"]) if "symbol" in evidence
                else f"anchor {code_span(evidence['anchor'])}"
            )
            modules = evidence["modules"]
            if not modules:
                modules = [None]
            for module in modules:
                module_status = statuses.get(module) if module else None
                fixture_seen = module and (module, evidence.get("symbol")) in observed
                if module_status is None:
                    status = "not run"
                elif fixture_seen:
                    status = f"fixture observed; module {module_status}"
                else:
                    status = f"module {module_status}; fixture not observed"
                    if module_status == "passed":
                        status += " (may be compiled out on this host)"
                requirement_id = evidence.get("requires")
                requirement = data["requirements"].get(requirement_id, {})
                if module_status is not None and requirement.get("missing") == "skip":
                    status += f"; requirement {requirement_id} may have skipped"
                note = " ".join(notes[(cell["backend"], cell["feature"])].split())
                if len(note) > 120:
                    note = note[:117].rstrip() + "..."
                lines.append("| " + " | ".join((
                    markdown_cell(cell["backend"]),
                    markdown_cell(cell["feature"]),
                    markdown_cell(label),
                    markdown_cell(module or "—"),
                    markdown_cell(status),
                    markdown_cell(note),
                )) + " |")
    return "\n".join(lines) + "\n", []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_const", const="check", dest="mode")
    modes.add_argument("--write", action="store_const", const="write", dest="mode")
    modes.add_argument("--json", action="store_const", const="json", dest="mode")
    modes.add_argument("--ci-report", metavar="LOG", dest="ci_report")
    parser.set_defaults(mode="check")
    parser.set_defaults(ci_report=None)
    parser.add_argument("--root", type=Path, help="repository root (default: script location)")
    parser.add_argument("--catalogue", help=f"catalogue path, relative to --root (default: {CATALOGUE})")
    arguments = parser.parse_args(argv)

    root = (arguments.root or Path(__file__).resolve().parents[1]).resolve()
    catalogue = arguments.catalogue or CATALOGUE
    catalogue_path = rooted_file(root, catalogue)
    catalogue_label = catalogue if not Path(catalogue).is_absolute() else str(catalogue_path)
    data, errors = load_catalogue(catalogue_path, catalogue_label)
    if data is not None:
        errors.extend(validate_catalogue(data, root, catalogue_label))
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    if arguments.mode == "json":
        try:
            sys.stdout.write(json.dumps(derived_report(data, root), indent=2, ensure_ascii=False) + "\n")
            sys.stdout.flush()
        except BrokenPipeError:
            sys.stdout = None
        return 0

    if arguments.ci_report is not None:
        log_path = rooted_file(root, arguments.ci_report)
        log_text = read_text(log_path)
        if log_text is None:
            print(f"{arguments.ci_report}: could not read test log", file=sys.stderr)
            return 1
        report, report_errors = render_ci_report(data, root, log_text)
        if report_errors:
            for error in report_errors:
                print(f"{arguments.ci_report}: {error}", file=sys.stderr)
            return 1
        summary = os.environ.get("GITHUB_STEP_SUMMARY")
        try:
            if summary:
                with open(summary, "a", encoding="utf-8") as handle:
                    handle.write(report)
            else:
                sys.stdout.write(report)
        except OSError as error:
            print(f"{arguments.ci_report}: could not write CI report: {error}", file=sys.stderr)
            return 1
        return 0

    rendered = render_catalogue(data, root).encode("utf-8")
    output_path = root / OUTPUT
    if arguments.mode == "write":
        try:
            output_path.write_bytes(rendered)
        except OSError as error:
            print(f"{OUTPUT}: could not write generated documentation: {error}", file=sys.stderr)
            return 1
        print(f"wrote {OUTPUT}")
        return 0

    try:
        current = output_path.read_bytes()
    except OSError as error:
        print(f"{OUTPUT}: could not read generated documentation: {error}", file=sys.stderr)
        return 1
    if current != rendered:
        print(f"{OUTPUT}: does not match rendered catalogue; run python3 tools/backend_capabilities.py --write",
              file=sys.stderr)
        return 1
    print(f"{OUTPUT}: matches rendered catalogue")
    return 0


if __name__ == "__main__":
    sys.exit(main())
