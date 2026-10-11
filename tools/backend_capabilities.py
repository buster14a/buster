#!/usr/bin/env python3
"""Validate and render the evidence-backed backend capability catalogue.

Usage:
    python3 tools/backend_capabilities.py --check
    python3 tools/backend_capabilities.py --write
    python3 tools/backend_capabilities.py --json
    python3 tools/backend_capabilities.py --self-test
"""

import argparse
import copy
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path
import posixpath

SCHEMA = "buster-backend-capabilities-v1"
CATALOGUE = "docs/backend-capabilities-v1.json"
OUTPUT = "docs/backend-capabilities.md"
STATUSES = ["supported", "partial", "unsupported-diagnostic", "not-applicable", "unknown"]
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
        for key, text in requirements.items():
            if not nonempty_string(key):
                errors.append(catalogue_error(label, "requirements", "keys must be non-empty strings"))
            if not nonempty_string(text):
                errors.append(catalogue_error(label, f"requirements.{key}", "must be a non-empty string"))

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
                    occurrence = re.compile(rf"\b{re.escape(symbol)}\b")
                    count = sum(len(occurrence.findall(text)) for text in test_texts.values())
                    if count < 2:
                        errors.append(catalogue_error(
                            label, f"{entry_location}.symbol",
                            f"{symbol!r} occurs {count} times under src/buster/tests; expected at least 2"))
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
        report_cell = {
            "backend": cell["backend"],
            "feature": cell["feature"],
            "status": cell["status"],
            "levels": [level for level in level_ids if level in levels],
            "diagnostics": diagnostics,
        }
        cells.append(report_cell)
        summary[cell["backend"]][cell["status"]] += 1
    return {"cells": cells, "summary": summary}


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
            "Evidence marked **requires X** is skipped (not passed) when X is absent. Requirements: "
            + "; ".join(f"{code_span(key)}: {value}" for key, value in requirements.items())
            + "."
        )
    else:
        lines.append("Evidence has no optional external requirements.")
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
            for evidence in cell.get("evidence", []):
                if "symbol" in evidence:
                    item = f"{code_span(evidence['symbol'])} in {code_span(evidence['file'])}"
                else:
                    item = f"anchor {code_span(evidence['anchor'])} in {code_span(evidence['file'])}"
                if "requires" in evidence:
                    item += f" (requires {code_span(evidence['requires'])})"
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
        "CI runs `--self-test` and `--check` to verify the catalogue and detect drift.",
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


class BackendCapabilityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.write("docs/backend.md", "backend documentation\n")
        self.write("src/buster/lib/backend.c", 'return "backend diagnostic";\n')
        self.write(
            "src/buster/tests/test.c",
            'UnitTestResult fixture_test(void) { return UNIT_TEST_PASS; }\n'
            'void register_test(void) { fixture_test(); }\n'
            '"backend diagnostic";\n',
        )
        self.data = {
            "schema": SCHEMA,
            "reviewed_revision": "a" * 40,
            "owner_issue": 309,
            "levels": [{"id": "encode", "name": "Exact encode"}],
            "statuses": list(STATUSES),
            "backends": [{
                "id": "fixture",
                "name": "Fixture backend",
                "kind": "direct-codegen",
                "doc": "docs/backend.md",
            }],
            "features": [{"id": "calls", "name": "Calls"}],
            "requirements": {"node": "Node.js is required; skipped when absent"},
            "cells": [{
                "backend": "fixture",
                "feature": "calls",
                "status": "supported",
                "evidence": [{
                    "file": "src/buster/tests/test.c",
                    "symbol": "fixture_test",
                    "provides": ["encode"],
                    "requires": "node",
                }],
                "diagnostics": [{
                    "file": "src/buster/lib/backend.c",
                    "text": "backend diagnostic",
                }],
            }],
        }

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def errors(self, mutate=None):
        data = copy.deepcopy(self.data)
        if mutate is not None:
            mutate(data)
        return validate_catalogue(data, self.root, "catalogue.json")

    def assert_error(self, errors, text):
        self.assertTrue(any(text in error for error in errors), errors)

    def test_schema_revision_and_exact_top_level_keys(self):
        self.assertEqual([], self.errors())
        for mutate, expected in (
            (lambda data: data.update(schema="wrong"), "schema: must equal"),
            (lambda data: data.update(reviewed_revision="bad"), "reviewed_revision: must be 40"),
            (lambda data: data.update(extra=True), "unknown key 'extra'"),
            (lambda data: data.pop("owner_issue"), "missing required key 'owner_issue'"),
        ):
            with self.subTest(expected=expected):
                self.assert_error(self.errors(mutate), expected)

    def test_unknown_keys_are_rejected_for_every_object_type(self):
        self.assertEqual([], self.errors())
        mutations = (
            lambda data: data["levels"][0].update(extra=True),
            lambda data: data["backends"][0].update(extra=True),
            lambda data: data["features"][0].update(extra=True),
            lambda data: data["cells"][0].update(extra=True),
            lambda data: data["cells"][0]["evidence"][0].update(extra=True),
            lambda data: data["cells"][0]["diagnostics"][0].update(extra=True),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                self.assert_error(self.errors(mutate), "unknown key 'extra'")

    def test_ids_statuses_backend_kind_and_doc(self):
        self.assertEqual([], self.errors())
        mutations = (
            (lambda data: data["levels"].append({"id": "encode", "name": "duplicate"}), "duplicate id 'encode'"),
            (lambda data: data["backends"].append(copy.deepcopy(data["backends"][0])), "duplicate id 'fixture'"),
            (lambda data: data["features"].append({"id": "calls", "name": "duplicate"}), "duplicate id 'calls'"),
            (lambda data: data.update(statuses=["supported"]), "statuses: must equal"),
            (lambda data: data["backends"][0].update(kind="other"), "kind: must be"),
            (lambda data: data["backends"][0].update(doc="missing.md"), "file does not exist: missing.md"),
        )
        for mutate, expected in mutations:
            with self.subTest(expected=expected):
                self.assert_error(self.errors(mutate), expected)

    def test_backend_feature_grid_is_complete_and_unique(self):
        self.assertEqual([], self.errors())
        self.assert_error(
            self.errors(lambda data: data["cells"].append(copy.deepcopy(data["cells"][0]))),
            "duplicate backend/feature cell",
        )
        self.assert_error(self.errors(lambda data: data["cells"].clear()), "missing backend/feature cell")

    def test_evidence_symbols_anchors_levels_and_requirements(self):
        self.assertEqual([], self.errors())
        anchor_data = copy.deepcopy(self.data)
        evidence = anchor_data["cells"][0]["evidence"][0]
        evidence.pop("symbol")
        evidence["anchor"] = "REGISTERED_ANCHOR"
        self.write("src/buster/tests/test.c", (self.root / "src/buster/tests/test.c").read_text()
                   + "REGISTERED_ANCHOR\n")
        self.assertEqual([], validate_catalogue(anchor_data, self.root, "catalogue.json"))

        mutations = (
            (lambda data: data["cells"][0]["evidence"][0].update(file="missing.c"), "file does not exist"),
            (lambda data: data["cells"][0]["evidence"][0].pop("symbol"), "exactly one of"),
            (lambda data: data["cells"][0]["evidence"][0].update(anchor="also"), "exactly one of"),
            (lambda data: data["cells"][0]["evidence"][0].update(provides=[]), "non-empty array"),
            (lambda data: data["cells"][0]["evidence"][0].update(provides=["unknown"]), "unknown level id"),
            (lambda data: data["cells"][0]["evidence"][0].update(provides=["encode", "encode"]), "must not contain duplicates"),
            (lambda data: data["cells"][0]["evidence"][0].update(requires="missing"), "unknown requirement"),
            (lambda data: data["cells"][0]["evidence"][0].update(symbol="missing_test"), "no UnitTestResult definition"),
        )
        for mutate, expected in mutations:
            with self.subTest(expected=expected):
                self.assert_error(self.errors(mutate), expected)

        self.write("src/buster/tests/test.c", "UnitTestResult fixture_test(void) { return UNIT_TEST_PASS; }\n")
        self.assert_error(self.errors(), "occurs 1 times under src/buster/tests")

        anchor_data["cells"][0]["evidence"][0]["anchor"] = "missing anchor"
        self.assert_error(validate_catalogue(anchor_data, self.root, "catalogue.json"), "literal anchor not found")

    def test_diagnostics_must_exist_and_report_tested_status(self):
        self.assertEqual([], self.errors())
        report = derived_report(self.data, self.root)
        self.assertTrue(report["cells"][0]["diagnostics"][0]["diagnostic_tested"])
        self.write("src/buster/tests/test.c", "UnitTestResult fixture_test(void) { }\nfixture_test();\n")
        self.assertEqual([], self.errors())
        self.assertFalse(derived_report(self.data, self.root)["cells"][0]["diagnostics"][0]["diagnostic_tested"])
        for mutate, expected in (
            (lambda data: data["cells"][0]["diagnostics"][0].update(file="missing.c"), "file does not exist"),
            (lambda data: data["cells"][0]["diagnostics"][0].update(text="missing diagnostic"),
             "literal diagnostic text not found"),
        ):
            with self.subTest(expected=expected):
                self.assert_error(self.errors(mutate), expected)

    def test_status_rules_accept_and_reject_each_status(self):
        self.assertEqual([], self.errors())
        valid_cells = {
            "supported": dict(evidence=[{"file": "src/buster/tests/test.c", "symbol": "fixture_test",
                                         "provides": ["encode"]}], diagnostics=[]),
            "partial": dict(evidence=[{"file": "src/buster/tests/test.c", "symbol": "fixture_test",
                                       "provides": ["encode"]}], diagnostics=[], note="Bounded evidence."),
            "unsupported-diagnostic": dict(evidence=[], diagnostics=[{"file": "src/buster/lib/backend.c",
                                                                       "text": "backend diagnostic"}]),
            "not-applicable": dict(evidence=[], diagnostics=[], note="Separate boundary."),
            "unknown": dict(evidence=[], diagnostics=[], note="Not audited."),
        }
        for status, values in valid_cells.items():
            def set_valid(data, selected=status, selected_values=values):
                data["cells"][0].update(status=selected, **copy.deepcopy(selected_values))
            with self.subTest(status=status):
                self.assertEqual([], self.errors(set_valid))

        invalid_cells = (
            ({"status": "supported", "evidence": [], "diagnostics": []}, "supported cells require"),
            ({"status": "partial", "evidence": [], "diagnostics": [], "note": "Need evidence."},
             "partial cells require at least one evidence"),
            ({"status": "partial", "evidence": [{"file": "src/buster/tests/test.c",
                                                  "symbol": "fixture_test", "provides": ["encode"]}],
              "diagnostics": []}, "partial cells require a note"),
            ({"status": "unsupported-diagnostic", "evidence": [], "diagnostics": []},
             "unsupported-diagnostic cells require"),
            ({"status": "unsupported-diagnostic", "evidence": [{"file": "src/buster/tests/test.c",
                                                                  "symbol": "fixture_test",
                                                                  "provides": ["encode"]}],
              "diagnostics": [{"file": "src/buster/lib/backend.c", "text": "backend diagnostic"}]},
             "unsupported-diagnostic cells must not contain evidence"),
            ({"status": "not-applicable", "evidence": [], "diagnostics": []},
             "not-applicable cells require a note"),
            ({"status": "not-applicable", "evidence": [{"file": "src/buster/tests/test.c",
                                                         "symbol": "fixture_test",
                                                         "provides": ["encode"]}],
              "diagnostics": [], "note": "Separate boundary."},
             "not-applicable cells must not contain evidence"),
            ({"status": "not-applicable", "evidence": [], "diagnostics": [
                {"file": "src/buster/lib/backend.c", "text": "backend diagnostic"}],
             "note": "Separate boundary."}, "not-applicable cells must not contain diagnostics"),
            ({"status": "unknown", "evidence": [], "diagnostics": []}, "unknown cells require a note"),
            ({"status": "unknown", "evidence": [{"file": "src/buster/tests/test.c",
                                                  "symbol": "fixture_test", "provides": ["encode"]}],
              "diagnostics": [], "note": "Not audited."}, "unknown cells must not contain evidence"),
        )
        for overrides, expected in invalid_cells:
            def set_invalid(data, selected=overrides):
                data["cells"][0].clear()
                data["cells"][0].update(backend="fixture", feature="calls", **copy.deepcopy(selected))
            with self.subTest(expected=expected):
                self.assert_error(self.errors(set_invalid), expected)

    def test_render_is_stable_and_report_contains_derived_levels_and_summary(self):
        rendered = render_catalogue(self.data, self.root)
        self.assertTrue(rendered.endswith("\n"))
        self.assertFalse(rendered.endswith("\n\n"))
        self.assertFalse(any(line.endswith((" ", "\t")) for line in rendered.splitlines()))
        self.assertIn("[Fixture backend](backend.md)", rendered)
        self.assertIn("diagnostic `backend diagnostic` (tested)", rendered)
        report = derived_report(self.data, self.root)
        self.assertEqual(["encode"], report["cells"][0]["levels"])
        self.assertEqual(1, report["summary"]["fixture"]["supported"])


def self_test():
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BackendCapabilityTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_const", const="check", dest="mode")
    modes.add_argument("--write", action="store_const", const="write", dest="mode")
    modes.add_argument("--json", action="store_const", const="json", dest="mode")
    modes.add_argument("--self-test", action="store_const", const="self-test", dest="mode")
    parser.set_defaults(mode="check")
    parser.add_argument("--root", type=Path, help="repository root (default: script location)")
    parser.add_argument("--catalogue", help=f"catalogue path, relative to --root (default: {CATALOGUE})")
    arguments = parser.parse_args(argv)
    if arguments.mode == "self-test":
        return self_test()

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
