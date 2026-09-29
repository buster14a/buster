#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NEW_SUPPORT = "52cf1a5ef3744ba5777b729aeed9be5a37f5310368c78d92a76499b08e26965f"


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def regex_once(path: str, pattern: str, replacement: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, lambda _match: replacement, text, count=1,
                             flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}")
    target.write_text(updated, encoding="utf-8")


# Full-census validator: retain both previously reviewed identities and admit
# exactly the proposed #1790 declaration identity.
replace_once(
    "tools/native_retirement_contract.py",
    'NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n',
    'NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n'
    f'ISSUE_1790_SUPPORT_CONTRACT_SHA256 = "{NEW_SUPPORT}"\n',
)
replace_once(
    "tools/native_retirement_contract.py",
    "            FULL_SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256)\n",
    "            FULL_SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256,\n"
    "            ISSUE_1790_SUPPORT_CONTRACT_SHA256)\n",
)
replace_once(
    "tools/native_retirement_contract_test.py",
    "        for digest in (contract.FULL_SUPPORT_CONTRACT_SHA256,\n"
    "                       contract.NEXT_SUPPORT_CONTRACT_SHA256):\n",
    "        for digest in (contract.FULL_SUPPORT_CONTRACT_SHA256,\n"
    "                       contract.NEXT_SUPPORT_CONTRACT_SHA256,\n"
    "                       contract.ISSUE_1790_SUPPORT_CONTRACT_SHA256):\n",
)

# Materializer: the old trusted implementation must be able to reconstruct the
# later policy candidate without relaxing identity checks.
replace_once(
    "tools/native_retirement_materializer.py",
    'NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n',
    'NEXT_SUPPORT_CONTRACT_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n'
    f'ISSUE_1790_SUPPORT_CONTRACT_SHA256 = "{NEW_SUPPORT}"\n',
)
replace_once(
    "tools/native_retirement_materializer.py",
    "            SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256):\n",
    "            SUPPORT_CONTRACT_SHA256, NEXT_SUPPORT_CONTRACT_SHA256,\n"
    "            ISSUE_1790_SUPPORT_CONTRACT_SHA256):\n",
)
replace_once(
    "tools/native_retirement_materializer_test.py",
    "                      (materializer.SUPPORT_CONTRACT_SHA256,\n"
    "                       materializer.NEXT_SUPPORT_CONTRACT_SHA256))\n",
    "                      (materializer.SUPPORT_CONTRACT_SHA256,\n"
    "                       materializer.NEXT_SUPPORT_CONTRACT_SHA256,\n"
    "                       materializer.ISSUE_1790_SUPPORT_CONTRACT_SHA256))\n",
)

# Performance binding: preserve historical evidence while admitting only the
# exact current and proposed reviewed support declarations.
replace_once(
    "tools/native_retirement_performance_binding.py",
    'NEXT_SUPPORT_DECLARATION_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n',
    'NEXT_SUPPORT_DECLARATION_SHA256 = "932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82"\n'
    f'ISSUE_1790_SUPPORT_DECLARATION_SHA256 = "{NEW_SUPPORT}"\n',
)
replace_once(
    "tools/native_retirement_performance_binding.py",
    "    if support_sha256 not in (SUPPORT_DECLARATION_SHA256,\n"
    "                             NEXT_SUPPORT_DECLARATION_SHA256):\n",
    "    if support_sha256 not in (SUPPORT_DECLARATION_SHA256,\n"
    "                             NEXT_SUPPORT_DECLARATION_SHA256,\n"
    "                             ISSUE_1790_SUPPORT_DECLARATION_SHA256):\n",
)
replace_once(
    "tools/native_retirement_performance_identity_test.py",
    "                      (binding.SUPPORT_DECLARATION_SHA256,\n"
    "                       binding.NEXT_SUPPORT_DECLARATION_SHA256))\n",
    "                      (binding.SUPPORT_DECLARATION_SHA256,\n"
    "                       binding.NEXT_SUPPORT_DECLARATION_SHA256,\n"
    "                       binding.ISSUE_1790_SUPPORT_DECLARATION_SHA256))\n",
)
replace_once(
    "tools/native_retirement_performance_eligibility_test.py",
    "                      (census.FULL_SUPPORT_CONTRACT_SHA256,\n"
    "                       census.NEXT_SUPPORT_CONTRACT_SHA256))\n",
    "                      (census.FULL_SUPPORT_CONTRACT_SHA256,\n"
    "                       census.NEXT_SUPPORT_CONTRACT_SHA256,\n"
    "                       census.ISSUE_1790_SUPPORT_CONTRACT_SHA256))\n",
)

replace_once(
    "docs/native-retirement-rebinding.md",
    "Old\n"
    "census/performance evidence remains bound to its original declaration digest;\n"
    "the matching manifest and exact declaration bytes are checked together.\n\n"
    "### Solo-maintainer authorization\n",
    "Old\n"
    "census/performance evidence remains bound to its original declaration digest;\n"
    "the matching manifest and exact declaration bytes are checked together.\n\n"
    "For #1790, a second source-only bootstrap preserves both previously reviewed\n"
    "support declaration digests and admits exactly\n"
    f"`{NEW_SUPPORT}` for the later desktop artifact-resolution policy change.\n"
    "It leaves `tests/ci_tools_test.py`, the support ledger, generated retirement\n"
    "bindings and the benchmark-service support pin unchanged. The separate policy\n"
    "transition may update those bytes only after this reader bootstrap is trusted.\n\n"
    "### Solo-maintainer authorization\n",
)

# Rebind the benchmark profile to the changed validator implementation while
# retaining its current reviewed support declaration until the policy PR.
binding_path = ROOT / "tools/native_retirement_performance_binding.py"
binding_sha = hashlib.sha256(binding_path.read_bytes()).hexdigest()
for relative in (
    "tools/bench_service/profiles/native-retirement-performance-v1.blocked",
    "tools/bench_service/queue.c",
):
    target = ROOT / relative
    text = target.read_text(encoding="utf-8")
    updated, count = re.subn(
        r"binding-validator-sha256=[0-9a-f]{64}",
        f"binding-validator-sha256={binding_sha}", text,
    )
    if count != 1:
        raise SystemExit(f"{relative}: expected one binding-validator pin, found {count}")
    target.write_text(updated, encoding="utf-8")

print(f"ISSUE_1790_SUPPORT_SHA256={NEW_SUPPORT}")
print(f"BINDING_VALIDATOR_SHA256={binding_sha}")

for temporary in (
    ROOT / ".github" / "scripts" / "patch_1790_bootstrap.py",
    ROOT / ".github" / "workflows" / "patch-1790-bootstrap.yml",
):
    temporary.unlink(missing_ok=True)
