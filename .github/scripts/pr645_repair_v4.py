#!/usr/bin/env python3
import hashlib
import re
import runpy
from pathlib import Path

root = Path(__file__).resolve().parents[2]
runpy.run_path(str(root / ".github/scripts/pr645_repair_v3.py"), run_name="__main__")

support = root / "docs/native-retirement-support-v1.tsv"
support_sha = hashlib.sha256(support.read_bytes()).hexdigest()
materializer_path = root / "tools/native_retirement_materializer.py"
materializer = materializer_path.read_text(encoding="utf-8")
materializer, count = re.subn(
    r'^(SUPPORT_CONTRACT_SHA256\s*=\s*)"[0-9a-f]{64}"\s*$',
    rf'\g<1>"{support_sha}"',
    materializer,
    count=1,
    flags=re.M,
)
if count != 1:
    raise SystemExit(f"materializer support trust-anchor replacement count: {count}")
materializer_path.write_text(materializer, encoding="utf-8")
