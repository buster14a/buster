#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
test_path = root / "tests" / "ci_tools_test.py"
ledger_path = root / "docs" / "native-retirement-support-v1.tsv"

test = test_path.read_text(encoding="utf-8")
old = '        self.assertNotRegex(text, r"(?m)^\\s*continue-on-error:")\n'
new = '''        continuation_steps = []
        for match in re.finditer(
                r"(?ms)^      - name: ([^\\n]+)\\n(.*?)(?=^      - name:|\\Z)", text):
            if re.search(r"(?m)^        continue-on-error: true$", match.group(2)):
                continuation_steps.append(match.group(1))
        self.assertEqual(continuation_steps, [
            "Attempt desktop log retention",
            "Retry desktop log retention after action resolution failure",
        ])
        self.assertEqual(len(re.findall(r"(?m)^\\s*continue-on-error:", text)), 2)
'''
if test.count(old) != 1:
    raise SystemExit("workflow continuation policy assertion not found exactly once")
test_path.write_text(test.replace(old, new, 1), encoding="utf-8")

test_bytes = test_path.read_bytes()
test_sha = hashlib.sha256(test_bytes).hexdigest()
ledger = ledger_path.read_text(encoding="utf-8")
pattern = re.compile(r"(?m)^tests/ci_tools_test\.py\tsupport-file\tdependency-only\t\d+\t[0-9a-f]{64}$")
replacement = f"tests/ci_tools_test.py\tsupport-file\tdependency-only\t{len(test_bytes)}\t{test_sha}"
ledger, count = pattern.subn(replacement, ledger)
if count != 1:
    raise SystemExit(f"support ledger row replacement count was {count}")
ledger_path.write_text(ledger, encoding="utf-8")
ledger_bytes = ledger_path.read_bytes()
ledger_sha = hashlib.sha256(ledger_bytes).hexdigest()

print(f"PROPOSED_TEST_BYTES={len(test_bytes)}")
print(f"PROPOSED_TEST_SHA256={test_sha}")
print(f"PROPOSED_SUPPORT_BYTES={len(ledger_bytes)}")
print(f"PROPOSED_SUPPORT_SHA256={ledger_sha}")
