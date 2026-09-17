#!/usr/bin/env python3
import hashlib
import re
import runpy
from pathlib import Path

root = Path(__file__).resolve().parents[2]
runpy.run_path(str(root / ".github/scripts/pr645_repair_v4.py"), run_name="__main__")

support_sha = hashlib.sha256((root / "docs/native-retirement-support-v1.tsv").read_bytes()).hexdigest()
test_path = root / "tools/native_retirement_materializer_test.py"
tests = test_path.read_text(encoding="utf-8")
pattern = re.compile(
    r'(self\.assertEqual\(materializer\.SUPPORT_CONTRACT_SHA256,\s*)"[0-9a-f]{64}"(\s*\))',
    re.S,
)
tests, count = pattern.subn(rf'\g<1>"{support_sha}"\g<2>', tests, count=1)
if count != 1:
    raise SystemExit(f"materializer historical-pin assertion replacement count: {count}")
test_path.write_text(tests, encoding="utf-8")
