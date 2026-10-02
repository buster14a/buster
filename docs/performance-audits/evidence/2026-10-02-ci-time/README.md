# CI evidence bundle — 2 October 2026

Read report.md first. The phase percentages apply to complete desktop matrices in two frozen-source samples, not all account usage, billed time, or CPU time.

raw/: 27 original GitHub artifacts, unchanged. verified_artifacts.csv lists their published and actual SHA-256 digests.
derived/: full-precision CSV/JSON phase, module, launch-record and partition ledgers; analyzer diagnostic records.

Reproduce using Python 3 (standard library only):
    python verify_artifacts.py
    python analyze.py

These commands only read ZIP/log/JSON files and write derived reports. They do not build the repository or execute tests. Run without Python -O because the analysis helper uses assertions for source/receipt checks.

This is a diagnostic report, not a substitute for Buster's independent qualification validators. Logged launches are not complete OS process counts. Module durations overlap. Unknown phase CPU and RSS are not inferred from wall duration, arena counters, or unrelated child records.
