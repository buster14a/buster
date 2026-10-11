#!/usr/bin/env python3
"""Synthetic tests for the backend capability catalogue validator."""

import copy
import tempfile
import unittest
from pathlib import Path

import backend_capabilities as capabilities


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
            "schema": capabilities.SCHEMA,
            "reviewed_revision": "a" * 40,
            "owner_issue": 309,
            "levels": [{"id": "encode", "name": "Exact encode"}],
            "statuses": list(capabilities.STATUSES),
            "backends": [{
                "id": "fixture",
                "name": "Fixture backend",
                "kind": "direct-codegen",
                "doc": "docs/backend.md",
            }],
            "features": [{"id": "calls", "name": "Calls"}],
            "requirements": {
                "node": {"text": "Node.js is required; skipped when absent", "missing": "skip"},
            },
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
        return capabilities.validate_catalogue(data, self.root, "catalogue.json")

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

    def test_requirement_schema_accepts_disposition_and_rejects_invalid_values(self):
        self.assertEqual([], self.errors())
        for mutate, expected in (
            (lambda data: data["requirements"].update(node="Node.js is required"),
             "requirements.node: expected an object"),
            (lambda data: data["requirements"]["node"].update(missing="ignore"),
             "requirements.node.missing: must be 'skip' or 'fail'"),
            (lambda data: data["requirements"]["node"].update(extra=True),
             "requirements.node: unknown key 'extra'"),
        ):
            with self.subTest(expected=expected):
                self.assert_error(self.errors(mutate), expected)

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
        self.assertEqual([], capabilities.validate_catalogue(anchor_data, self.root, "catalogue.json"))

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
        self.assert_error(
            capabilities.validate_catalogue(anchor_data, self.root, "catalogue.json"),
            "literal anchor not found",
        )

    def test_diagnostics_must_exist_and_report_tested_status(self):
        self.assertEqual([], self.errors())
        report = capabilities.derived_report(self.data, self.root)
        self.assertTrue(report["cells"][0]["diagnostics"][0]["diagnostic_tested"])
        self.write("src/buster/tests/test.c", "UnitTestResult fixture_test(void) { }\nfixture_test();\n")
        self.assertEqual([], self.errors())
        self.assertFalse(
            capabilities.derived_report(self.data, self.root)["cells"][0]["diagnostics"][0]["diagnostic_tested"])
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
        rendered = capabilities.render_catalogue(self.data, self.root)
        self.assertTrue(rendered.endswith("\n"))
        self.assertFalse(rendered.endswith("\n\n"))
        self.assertFalse(any(line.endswith((" ", "\t")) for line in rendered.splitlines()))
        self.assertIn("[Fixture backend](backend.md)", rendered)
        self.assertIn("Evidence marked **requires X** depends on tool X.", rendered)
        self.assertIn("requires `node`; missing: skip", rendered)
        self.assertIn("diagnostic `backend diagnostic` (tested)", rendered)
        report = capabilities.derived_report(self.data, self.root)
        self.assertEqual(["encode"], report["cells"][0]["levels"])
        self.assertEqual(1, report["summary"]["fixture"]["supported"])
        self.assertEqual(self.data["requirements"], report["requirements"])


if __name__ == "__main__":
    unittest.main()
