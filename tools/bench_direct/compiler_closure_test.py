#!/usr/bin/env python3
"""Replay bytes exported by the actual native closure producer/consumer on hosted CI."""
import hashlib
import json
import pathlib
import sys
import unittest

import compiler_receipt

class NativeClosureReplayTest(unittest.TestCase):
    def test_actual_native_source_driver_harness_and_receipts(self):
        directory = pathlib.Path(sys.argv[1])
        records = {}
        manifests = {}
        for operation in ("snapshot", "restore", "verify"):
            records[operation] = json.loads((directory / (operation + ".json")).read_text())
            manifests[operation] = (directory / (operation + ".json.manifest.tsv")).read_bytes()
        current = {"identity": {"base": records["snapshot"]["base"], "base_tree": records["snapshot"]["base_tree"]},
                   "binaries": {"baseline": {"sha256": hashlib.sha256(b"baseline compiler bytes\n").hexdigest()}},
                   "closure": {"policy": "snapshot-v1", "fallback": None, **records}}
        self.assertEqual(compiler_receipt.validate_closure(current, manifests), [])
        fields = manifests["snapshot"].decode().splitlines()
        self.assertTrue(any(line.endswith("\tfixture-dependency.h") for line in fields))
        self.assertTrue(any(line.endswith("\ttools/bootstrap_driver.sh") for line in fields))
        self.assertTrue(any(line.startswith("binding\tbootstrap_artifact\tposix/") for line in fields))
        self.assertTrue(any(line.startswith("build\tF\t") and line.endswith("\tthroughput-tools/throughput") for line in fields))
        self.assertTrue(compiler_receipt.validate_closure(current, dict(manifests, verify=b"interrupted")))
        records["restore"]["bootstrap_artifact_sha256"] = "0" * 64
        self.assertTrue(compiler_receipt.validate_closure(current, manifests))

    def test_actual_three_arm_acquisition_exports_bound_closure_and_binaries(self):
        directory = pathlib.Path(sys.argv[1])
        prepared = json.loads((directory / "prepared.json").read_text())
        self.assertEqual((prepared["state"], prepared["arm_count"], prepared["policy"]), ("complete", 3, "snapshot-v1"))
        self.assertEqual(prepared["ownership_schema"], "buster-native-qualification-supervisor-v1")
        self.assertIs(prepared["cleanup_proven"], True)
        for role in ("baseline", "candidate", "candidate2"):
            raw = (directory / (role + ".binary.json")).read_bytes()
            binary = json.loads(raw)
            cache = (directory / (role + ".CMakeCache.txt")).read_bytes()
            self.assertEqual(prepared[role + "_receipt_sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(prepared[role + "_cache_sha256"], hashlib.sha256(cache).hexdigest())
            self.assertEqual((binary["state"], binary["sha256"], binary["bytes"], binary["mode"]),
                             ("complete", prepared[role + "_sha256"], prepared[role + "_bytes"], prepared[role + "_mode"]))
        records, manifests = {}, {}
        for operation in ("snapshot", "restore", "verify"):
            records[operation] = json.loads((directory / ("closure-" + operation + ".json")).read_text())
            manifests[operation] = (directory / ("closure-" + operation + ".json.manifest.tsv")).read_bytes()
        receipt = {"identity": {key: prepared[key] for key in ("base", "base_tree")},
                   "binaries": {"baseline": {"sha256": prepared["baseline_sha256"]}},
                   "preparation_policy": "snapshot-v1",
                   "closure": {"policy": "snapshot-v1", "fallback": None, **records}}
        self.assertEqual(compiler_receipt.validate_closure(receipt, manifests, expected_policy="snapshot-v1"), [])
        ledger = (directory / "phases.tsv").read_bytes()
        workload = (directory / "prepared.workload.tsv").read_bytes()
        manifest = (directory / "prepared.manifest.tsv").read_bytes()
        self.assertEqual(prepared["ledger_sha256"], hashlib.sha256(ledger).hexdigest())
        self.assertEqual(prepared["frozen_workload_sha256"], hashlib.sha256(workload).hexdigest())
        self.assertEqual(prepared["prepared_manifest_sha256"], hashlib.sha256(manifest).hexdigest())
        self.assertIn(b"candidate2-build", ledger)
        self.assertIn(b"closure-verify", ledger)
        self.assertNotIn(b"-lab", ledger)
        self.assertNotIn(b"-throughput", ledger)

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
