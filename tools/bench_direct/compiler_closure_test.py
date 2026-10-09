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

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
