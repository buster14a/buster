#!/usr/bin/env python3
"""Bind native-retirement acceptance to one immutable MIR candidate."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Iterable

from native_retirement_candidate_core import (
    ACCEPTANCE_SCHEMA,
    BACKEND,
    CandidateError,
    GATE_BINARY,
    IDENTITY_SCHEMA,
    RECEIPT_SCHEMA,
    REQUIRED_BINARIES,
    REQUIRED_GATES,
    canonical_bytes,
    load_json,
    validate_identity,
)
from native_retirement_candidate_io import _atomic_write, verify

def write_identity(options: argparse.Namespace) -> None:
    binaries = load_json(options.trusted_binaries)
    document = {
        "schema": IDENTITY_SCHEMA,
        "candidate_commit": options.candidate_commit,
        "candidate_tree": options.candidate_tree,
        "checkout_commit": options.checkout_commit,
        "candidate_source": options.candidate_source,
        "backend_identity": BACKEND,
        "census_contract_sha256": options.census_contract_sha256,
        "gates_contract_sha256": options.gates_contract_sha256,
        "trusted_binaries": binaries,
    }
    validate_identity(document)
    _atomic_write(options.output, canonical_bytes(document))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    check = subparsers.add_parser("verify", help="verify and atomically publish exact-candidate acceptance")
    check.add_argument("--identity", required=True, type=Path)
    check.add_argument("--receipts", required=True, type=Path)
    check.add_argument("--artifacts", required=True, type=Path)
    check.add_argument("--output", required=True, type=Path)
    identity = subparsers.add_parser("write-identity", help="write a validated immutable candidate identity")
    identity.add_argument("--candidate-commit", required=True)
    identity.add_argument("--candidate-tree", required=True)
    identity.add_argument("--checkout-commit", required=True)
    identity.add_argument("--candidate-source", required=True)
    identity.add_argument("--trusted-binaries", required=True, type=Path)
    identity.add_argument("--census-contract-sha256", required=True)
    identity.add_argument("--gates-contract-sha256", required=True)
    identity.add_argument("--output", required=True, type=Path)
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    options = build_parser().parse_args(argv)
    try:
        if options.command == "verify":
            verify(options.identity, options.receipts, options.artifacts, options.output)
        else:
            write_identity(options)
    except CandidateError as error:
        print(f"native-retirement candidate verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
