#!/usr/bin/env python3
"""Replay unmodified production phase journals through the real analyzer.

Set BUSTER_PHASE_BRIDGE_INPUT to an unpacked desktop artifact and SOURCE,
TREE, RUN and ATTEMPT to its independently selected producer identity.
RETAIN optionally preserves every negative copy; originals are never edited.
Replay does not replace live compiler/coverage qualification in ci_summary.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

import ci_matrix_phases as phases
from ci_matrix_phases_test import fixture


class TerminalResultTests(unittest.TestCase):
    def test_terminal_requires_integer_success(self):
        for result in (False, True, 0.0, None, "0", 1):
            with self.subTest(result=repr(result)), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                coverage = fixture(root)
                terminal = phases.read(root / "terminal.json")
                terminal["result"] = result
                (root / "terminal.json").write_text(json.dumps(terminal) + "\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "failed/incomplete driver publication"):
                    phases.analyze(root, coverage)


@unittest.skipUnless(os.environ.get("BUSTER_PHASE_BRIDGE_INPUT"), "requires a production desktop artifact")
class ProductionPhaseBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.artifact = Path(os.environ["BUSTER_PHASE_BRIDGE_INPUT"]).resolve()
        cls.root = cls.artifact / "matrix-phases"
        cls.coverage = phases.read(cls.artifact / "coverage.json")
        cls.plan = phases.read(cls.root / "plan.json")
        cls.environment = {
            "GITHUB_SHA": os.environ["BUSTER_PHASE_BRIDGE_SOURCE"],
            "GITHUB_REPOSITORY": "buster14a/buster",
            "GITHUB_RUN_ID": os.environ["BUSTER_PHASE_BRIDGE_RUN"],
            "GITHUB_RUN_ATTEMPT": os.environ["BUSTER_PHASE_BRIDGE_ATTEMPT"],
        }
        if cls.plan["identity"]["source_tree"] != os.environ["BUSTER_PHASE_BRIDGE_TREE"]:
            raise ValueError("producer tree differs from independently selected source")
        cls.before = {p.name: p.read_bytes() for p in cls.root.iterdir() if p.is_file()}
        cls.coverage_before = (cls.artifact / "coverage.json").read_bytes()
        retained = os.environ.get("BUSTER_PHASE_BRIDGE_RETAIN")
        if retained:
            cls.copies = Path(retained).resolve()
            cls.copies.mkdir(parents=True, exist_ok=False)
        else:
            temporary = tempfile.TemporaryDirectory(prefix="buster-phase-bridge-")
            cls.addClassCleanup(temporary.cleanup)
            cls.copies = Path(temporary.name)
        print("PHASE_BRIDGE_PRODUCER " + json.dumps(cls.plan["identity"], sort_keys=True), flush=True)
        for name, content in sorted(cls.before.items()):
            print("PHASE_BRIDGE_RAW " + name + " " + hashlib.sha256(content).hexdigest(), flush=True)

    @classmethod
    def tearDownClass(cls):
        after = {p.name: p.read_bytes() for p in cls.root.iterdir() if p.is_file()}
        if after != cls.before or (cls.artifact / "coverage.json").read_bytes() != cls.coverage_before:
            raise AssertionError("production input bytes were modified")

    def analyze(self, root=None):
        return phases.analyze(root or self.root, copy.deepcopy(self.coverage), self.environment)

    def test_unmodified_production_journal(self):
        report = self.analyze()
        self.assertTrue(report["complete"])
        self.assertEqual(report["identity"], self.plan["identity"])
        self.assertEqual(len(report["events"]), len(self.plan["tasks"]))
        for event in report["events"]:
            self.assertIs(type(event["result"]), int)
            self.assertEqual(event["result"], 0)
            self.assertEqual(event["cpu_time"], "unknown")
            self.assertEqual(event["peak_rss"], "unknown")

    def test_legitimate_empty_and_inapplicable_populations(self):
        report = self.analyze()
        # A complete zero-tree journal is not admitted. Empty argv is legitimate
        # in the plan for a late-bound pooled command, not in its actual record.
        self.assertTrue(any(task["argv"] == [] for task in self.plan["tasks"]))
        compile_only = [tree for tree in report["trees"] if tree["compiler"] != "clang"]
        self.assertTrue(compile_only)
        for tree in compile_only:
            self.assertFalse(any(task["tree"] == tree["id"] and task["phase"] == "test" for task in self.plan["tasks"]))
            self.assertEqual(tree["elapsed_us"]["test"], 0)
        callbacks = [event for event in report["events"] if event.get("authority") == "driver_callback"]
        self.assertTrue(callbacks)
        for event in callbacks:
            # A real in-driver callback has no child-process result population.
            self.assertNotIn("spawned", event)
            self.assertNotIn("platform_status", event)
        for event in report["events"]:
            if event.get("authority") != "driver_callback":
                self.assertEqual(event["ctest_jobs"], "not-applicable")

    def test_bounded_negative_copies(self):
        task = next(task for task in self.plan["tasks"] if task["phase"] == "configure")
        start = next(self.root.glob(task["id"] + ".*.start.json")).name
        end = next(self.root.glob(task["id"] + ".*.end.json")).name
        cases = (
            ("missing-identity", "plan.json", lambda value: value["tasks"][0].pop("id")),
            ("duplicate-identity", "plan.json", lambda value: value["tasks"].append(copy.deepcopy(value["tasks"][0]))),
            ("mismatched-version", "plan.json", lambda value: value.update(schema="buster-desktop-phases-v999")),
            ("mismatched-source", "plan.json", lambda value: value["identity"].update(source_revision="0" * 40)),
            ("unsuccessful-child", end, lambda value: value.update(state="failure", result=1, platform_status=7)),
            ("false-child-success", end, lambda value: value.update(platform_status=7)),
            ("null-child-result", end, lambda value: value.update(result=None)),
            ("unmeasured-is-not-zero", end, lambda value: value.update(cpu_time=0, peak_rss=0)),
            ("boolean-terminal-result", "terminal.json", lambda value: value.update(result=False)),
            ("floating-terminal-result", "terminal.json", lambda value: value.update(result=0.0)),
        )
        for label, name, mutate in cases:
            with self.subTest(case=label):
                root = shutil.copytree(self.root, self.copies / label)
                value = phases.read(root / name)
                mutate(value)
                (root / name).write_text(json.dumps(value) + "\n", encoding="utf-8")
                # Parsing succeeds; that is not evidence or execution acceptance.
                self.assertIsInstance(phases.read(root / name), dict)
                with self.assertRaises(ValueError):
                    self.analyze(root)
        for label in ("missing-start", "duplicate-start", "truncated-end", "empty-end"):
            with self.subTest(case=label):
                root = shutil.copytree(self.root, self.copies / label)
                if label == "missing-start":
                    (root / start).unlink()
                elif label == "duplicate-start":
                    shutil.copyfile(root / start, root / (task["id"] + ".duplicate.start.json"))
                else:
                    data = (root / end).read_bytes()
                    (root / end).write_bytes(data[:-1] if label == "truncated-end" else b"")
                with self.assertRaises(ValueError):
                    self.analyze(root)


if __name__ == "__main__":
    unittest.main()
