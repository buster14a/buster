#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

import native_retirement_candidate as candidate

C = "1" * 40
T = "2" * 40
M = "4" * 64
G = "5" * 64


def binary_digest(binary_id: str) -> str:
    return hashlib.sha256(("binary:" + binary_id).encode()).hexdigest()


def binaries(**updates):
    result = {}
    for binary_id in candidate.REQUIRED_BINARIES:
        result[binary_id] = {
            "kind": "verifier" if binary_id == "source-verifier" else "compiler",
            "target": binary_id,
            "sha256": binary_digest(binary_id),
            "bytes": 4096,
            "build_commit": C,
            "build_tree": T,
        }
    result.update(updates)
    return result


def identity(**updates):
    value = {
        "schema": candidate.IDENTITY_SCHEMA,
        "candidate_commit": C,
        "candidate_tree": T,
        "checkout_commit": C,
        "candidate_source": "pull_request_head",
        "backend_identity": candidate.BACKEND,
        "census_contract_sha256": M,
        "gates_contract_sha256": G,
        "trusted_binaries": binaries(),
    }
    value.update(updates)
    return value


def details(gate: str):
    if gate == "census-508":
        return {"contract_issue": 508, "complete": True, "manifest_sha256": M,
                "row_count": 77184, "supported_row_regressions": 0}
    if gate.startswith("strict-"):
        return {"contract_issue": 509, "semantic_complete": True, "direct_fallback_invocations": 0}
    if gate.startswith("sanitizer-"):
        return {"contract_issue": 509, "sanitizer_enabled": True, "failures": 0}
    if gate == "repeated-self-host-509":
        return {"contract_issue": 509, "iterations": 3, "fixed_point": True}
    if gate == "semantic-509":
        return {"contract_issue": 509, "archived_direct_oracle": True, "clang_oracle": True, "failures": 0}
    if gate == "production-direct-reachability":
        return {"production_direct_native_reachable": 0, "direct_fallback_invocations": 0}
    if gate == "final-change-comparison-512":
        return {"contract_issue": 512, "comparison_candidate_commit": C,
                "comparison_candidate_tree": T, "passed": True}
    raise AssertionError(gate)


def receipt(gate: str, artifact_path: str, artifact: bytes):
    binary_id = candidate.GATE_BINARY[gate]
    return {
        "schema": candidate.RECEIPT_SCHEMA,
        "gate": gate,
        "status": "success",
        "candidate_commit": C,
        "candidate_tree": T,
        "backend_identity": candidate.BACKEND,
        "census_contract_sha256": M,
        "gates_contract_sha256": G,
        "producer_commit": C,
        "producer_tree": T,
        "trusted_binary_id": binary_id,
        "trusted_binary_sha256": binary_digest(binary_id),
        "artifact_path": artifact_path,
        "artifact_bytes": len(artifact),
        "artifact_sha256": hashlib.sha256(artifact).hexdigest(),
        "workflow_run_id": "12345",
        "workflow_run_attempt": 1,
        "details": details(gate),
    }


class CandidateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.identity = self.root / "identity.json"
        self.receipts = self.root / "receipts"
        self.artifacts = self.root / "artifacts-root"
        self.receipts.mkdir()
        self.artifacts.mkdir()
        self.output = self.root / "acceptance.json"
        self.identity.write_text(json.dumps(identity()), encoding="utf-8")
        for gate in candidate.REQUIRED_GATES:
            relative = f"evidence/{gate}.bin"
            artifact = ("artifact:" + gate).encode()
            path = self.artifacts / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(artifact)
            (self.receipts / (gate + ".json")).write_text(
                json.dumps(receipt(gate, relative, artifact)), encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def verify(self):
        return candidate.verify(self.identity, self.receipts, self.artifacts, self.output)

    def assert_rejected(self, part):
        with self.assertRaisesRegex(candidate.CandidateError, part):
            self.verify()

    def read_receipt(self, gate):
        path = self.receipts / (gate + ".json")
        return path, json.loads(path.read_text())

    def test_valid_exact_candidate(self):
        result = self.verify()
        self.assertEqual(result["candidate_commit"], C)
        self.assertEqual(set(result["receipt_sha256"]), candidate.REQUIRED_GATES)
        self.assertEqual(json.loads(self.output.read_text()), result)
        self.assertFalse(any(self.root.glob("acceptance.json.partial-*")))

    def test_synthetic_merge_checkout_is_rejected(self):
        self.identity.write_text(json.dumps(identity(checkout_commit="7" * 40)), encoding="utf-8")
        self.assert_rejected("synthetic merge")

    def test_predecessor_receipt_is_rejected(self):
        path, value = self.read_receipt("semantic-509")
        value["producer_commit"] = "8" * 40
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("predecessor")

    def test_predecessor_trusted_binary_is_rejected(self):
        value = identity()
        value["trusted_binaries"]["linux-x86_64"]["build_commit"] = "8" * 40
        self.identity.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("predecessor")

    def test_stale_platform_compiler_is_rejected(self):
        path, value = self.read_receipt("strict-macos-aarch64")
        value["trusted_binary_sha256"] = "9" * 64
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("stale or foreign")

    def test_wrong_platform_compiler_is_rejected(self):
        path, value = self.read_receipt("strict-windows-aarch64")
        value["trusted_binary_id"] = "windows-x86_64"
        value["trusted_binary_sha256"] = binary_digest("windows-x86_64")
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("expected 'windows-aarch64'")

    def test_artifact_digest_is_verified(self):
        (self.artifacts / "evidence" / "semantic-509.bin").write_bytes(b"replacement")
        self.assert_rejected("byte count differs|SHA-256 differs")

    def test_artifact_symlink_is_rejected(self):
        path = self.artifacts / "evidence" / "semantic-509.bin"
        target = self.artifacts / "evidence" / "target.bin"
        target.write_bytes(path.read_bytes())
        path.unlink()
        path.symlink_to(target.name)
        self.assert_rejected("without following links")

    def test_direct_reachability_is_rejected(self):
        path, value = self.read_receipt("production-direct-reachability")
        value["details"]["production_direct_native_reachable"] = 1
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("direct native code is reachable")

    def test_missing_gate_is_rejected(self):
        (self.receipts / "census-508.json").unlink()
        self.assert_rejected("missing required gates")

    def test_duplicate_gate_is_rejected(self):
        original = self.receipts / "semantic-509.json"
        (self.receipts / "duplicate.json").write_bytes(original.read_bytes())
        self.assert_rejected("duplicate gate")

    def test_duplicate_artifact_path_is_rejected(self):
        path, value = self.read_receipt("strict-linux-aarch64")
        other = json.loads((self.receipts / "strict-linux-x86_64.json").read_text())
        artifact = self.artifacts / other["artifact_path"]
        value["artifact_path"] = other["artifact_path"]
        value["artifact_bytes"] = artifact.stat().st_size
        value["artifact_sha256"] = hashlib.sha256(artifact.read_bytes()).hexdigest()
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("duplicate artifact path")

    def test_stale_census_manifest_is_rejected(self):
        path, value = self.read_receipt("census-508")
        value["details"]["manifest_sha256"] = "a" * 64
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("stale census manifest")

    def test_final_change_must_name_exact_candidate_tree(self):
        path, value = self.read_receipt("final-change-comparison-512")
        value["details"]["comparison_candidate_tree"] = "b" * 40
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected("used a predecessor")

    def test_failure_does_not_replace_valid_output(self):
        original = b"previous-valid-acceptance\n"
        self.output.write_bytes(original)
        (self.receipts / "semantic-509.json").unlink()
        self.assert_rejected("missing required gates")
        self.assertEqual(self.output.read_bytes(), original)
        self.assertFalse(any(self.root.glob("acceptance.json.partial-*")))

    def test_duplicate_json_member_is_rejected(self):
        self.identity.write_text('{"schema":"x","schema":"y"}', encoding="utf-8")
        self.assert_rejected("duplicate JSON member")

    def test_unknown_receipt_member_is_rejected(self):
        path, value = self.read_receipt("semantic-509")
        value["untrusted"] = True
        path.write_text(json.dumps(value), encoding="utf-8")
        self.assert_rejected(r"unknown=\['untrusted'\]")

    def test_receipt_symlink_is_rejected(self):
        path = self.receipts / "semantic-509.json"
        target = self.receipts / "target.txt"
        target.write_bytes(path.read_bytes())
        path.unlink()
        path.symlink_to(target.name)
        self.assert_rejected("regular non-link")


if __name__ == "__main__":
    unittest.main()
