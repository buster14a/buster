#!/usr/bin/env python3
from __future__ import annotations

from typing import Any

from native_retirement_candidate_core import (
    BACKEND,
    GATE_BINARY,
    RECEIPT_SCHEMA,
    REQUIRED_GATES,
    _boolean,
    _commit,
    _digest,
    _exact_keys,
    _fail,
    _integer,
    _relative_path,
    _string,
    _token,
)

def _same_identity(receipt: dict[str, Any], identity: dict[str, Any]) -> None:
    for key in ("candidate_commit", "candidate_tree", "backend_identity", "census_contract_sha256", "gates_contract_sha256"):
        if receipt.get(key) != identity[key]:
            _fail(f"{key}: receipt has {receipt.get(key)!r}, expected {identity[key]!r}")
    if _commit(receipt, "producer_commit") != identity["candidate_commit"] or _commit(receipt, "producer_tree") != identity["candidate_tree"]:
        _fail("producer identity is a predecessor or unrelated commit/tree")


def _detail_keys(details: dict[str, Any], expected: set[str], gate: str) -> None:
    _exact_keys(details, expected, f"{gate}.details")


def validate_receipt(receipt: dict[str, Any], identity: dict[str, Any]) -> tuple[str, str]:
    expected = {
        "schema", "gate", "status", "candidate_commit", "candidate_tree", "backend_identity",
        "census_contract_sha256", "gates_contract_sha256", "producer_commit", "producer_tree",
        "trusted_binary_id", "trusted_binary_sha256", "artifact_path", "artifact_bytes", "artifact_sha256",
        "workflow_run_id", "workflow_run_attempt", "details",
    }
    _exact_keys(receipt, expected, "receipt")
    if receipt.get("schema") != RECEIPT_SCHEMA:
        _fail(f"receipt schema: expected {RECEIPT_SCHEMA}")
    gate = _string(receipt, "gate")
    if gate not in REQUIRED_GATES:
        _fail(f"unknown gate: {gate}")
    if receipt.get("status") != "success":
        _fail(f"{gate}: status is not success")
    _same_identity(receipt, identity)
    binary_id = _token(receipt, "trusted_binary_id")
    expected_binary = GATE_BINARY[gate]
    if binary_id != expected_binary:
        _fail(f"{gate}: trusted binary {binary_id!r}, expected {expected_binary!r}")
    binary = identity["trusted_binaries"][binary_id]
    if _digest(receipt, "trusted_binary_sha256") != binary["sha256"]:
        _fail(f"{gate}: stale or foreign trusted binary")
    artifact_path = _relative_path(receipt, "artifact_path")
    _integer(receipt, "artifact_bytes", 1)
    _digest(receipt, "artifact_sha256")
    _token(receipt, "workflow_run_id")
    _integer(receipt, "workflow_run_attempt", 1)
    details = receipt.get("details")
    if not isinstance(details, dict):
        _fail(f"{gate}: details must be an object")

    if gate == "census-508":
        _detail_keys(details, {"contract_issue", "complete", "manifest_sha256", "row_count", "supported_row_regressions"}, gate)
        if _integer(details, "contract_issue", 1) != 508 or not _boolean(details, "complete"):
            _fail("census-508: incomplete or wrong contract")
        if _digest(details, "manifest_sha256") != identity["census_contract_sha256"]:
            _fail("census-508: stale census manifest")
        _integer(details, "row_count", 1)
        if _integer(details, "supported_row_regressions", 0) != 0:
            _fail("census-508: supported rows regressed or became gaps")
    elif gate.startswith("strict-"):
        _detail_keys(details, {"contract_issue", "semantic_complete", "direct_fallback_invocations"}, gate)
        if _integer(details, "contract_issue", 1) != 509 or not _boolean(details, "semantic_complete"):
            _fail(f"{gate}: incomplete or wrong #509 semantic gate")
        if _integer(details, "direct_fallback_invocations", 0) != 0:
            _fail(f"{gate}: direct fallback was reached")
    elif gate.startswith("sanitizer-"):
        _detail_keys(details, {"contract_issue", "sanitizer_enabled", "failures"}, gate)
        if _integer(details, "contract_issue", 1) != 509 or not _boolean(details, "sanitizer_enabled"):
            _fail(f"{gate}: required sanitizer evidence is absent")
        if _integer(details, "failures", 0) != 0:
            _fail(f"{gate}: sanitizer failures are non-zero")
    elif gate == "repeated-self-host-509":
        _detail_keys(details, {"contract_issue", "iterations", "fixed_point"}, gate)
        if _integer(details, "contract_issue", 1) != 509 or _integer(details, "iterations", 3) < 3 or not _boolean(details, "fixed_point"):
            _fail("repeated-self-host-509: no repeated fixed point")
    elif gate == "semantic-509":
        _detail_keys(details, {"contract_issue", "archived_direct_oracle", "clang_oracle", "failures"}, gate)
        if _integer(details, "contract_issue", 1) != 509:
            _fail("semantic-509: wrong contract issue")
        if not _boolean(details, "archived_direct_oracle") or not _boolean(details, "clang_oracle"):
            _fail("semantic-509: both archived direct and Clang oracles are required")
        if _integer(details, "failures", 0) != 0:
            _fail("semantic-509: semantic failures are non-zero")
    elif gate == "production-direct-reachability":
        _detail_keys(details, {"production_direct_native_reachable", "direct_fallback_invocations"}, gate)
        if _integer(details, "production_direct_native_reachable", 0) != 0:
            _fail("production-direct-reachability: direct native code is reachable")
        if _integer(details, "direct_fallback_invocations", 0) != 0:
            _fail("production-direct-reachability: fallback was invoked")
    elif gate == "final-change-comparison-512":
        _detail_keys(details, {"contract_issue", "comparison_candidate_commit", "comparison_candidate_tree", "passed"}, gate)
        if _integer(details, "contract_issue", 1) != 512:
            _fail("final-change-comparison-512: wrong contract issue")
        if _commit(details, "comparison_candidate_commit") != identity["candidate_commit"] or _commit(details, "comparison_candidate_tree") != identity["candidate_tree"]:
            _fail("final-change-comparison-512: comparison used a predecessor")
        if not _boolean(details, "passed"):
            _fail("final-change-comparison-512: comparison did not pass")
    return gate, artifact_path
