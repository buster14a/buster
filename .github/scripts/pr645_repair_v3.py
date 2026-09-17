#!/usr/bin/env python3
import csv
import hashlib
import re
import runpy
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parents[2]
runpy.run_path(str(root / ".github/scripts/pr645_repair.py"), run_name="__main__")

support_path = root / "docs/native-retirement-support-v1.tsv"
with support_path.open(encoding="utf-8", newline="") as stream:
    support_rows = list(csv.DictReader(stream, delimiter="\t"))
input_count = len(support_rows)
role_counts = Counter(row["role"] for row in support_rows)
subject_count = role_counts["subject"]

applicability_path = root / "docs/native-retirement-applicability-v1.tsv"
applicability_sha = hashlib.sha256(applicability_path.read_bytes()).hexdigest()

contract_path = root / "tools/native_retirement_contract.py"
contract = contract_path.read_text(encoding="utf-8")
contract, count = re.subn(
    r'^(FULL_APPLICABILITY_LEDGER_SHA256\s*=\s*)"[0-9a-f]{64}"\s*$',
    rf'\g<1>"{applicability_sha}"',
    contract,
    count=1,
    flags=re.M,
)
if count != 1:
    raise SystemExit(f"applicability digest replacement count: {count}")
contract, count = re.subn(
    r'(manifest\.get\("inputs"\)\s*==\s*)"\d+"',
    rf'\g<1>"{input_count}"',
    contract,
    count=1,
)
if count != 1:
    raise SystemExit(f"full-profile input literal replacement count: {count}")
contract_path.write_text(contract, encoding="utf-8")

# Keep the forged-profile test past the authenticated population gates so it
# continues to prove that an incomplete subject map fails with the named error.
test_path = root / "tools/native_retirement_contract_test.py"
tests = test_path.read_text(encoding="utf-8")
match = re.search(
    r'(    def test_full_profile_claim_cannot_forge_the_production_population\(self\):.*?)(?=\n    def |\Z)',
    tests,
    re.S,
)
if not match:
    raise SystemExit("forged full-profile method not found")
block = match.group(1)
block = re.sub(r'"inputs": "\d+"', f'"inputs": "{input_count}"', block)
block = re.sub(r'"subjects": "\d+"', f'"subjects": "{subject_count}"', block)
tests = tests[:match.start(1)] + block + tests[match.end(1):]
test_path.write_text(tests, encoding="utf-8")
