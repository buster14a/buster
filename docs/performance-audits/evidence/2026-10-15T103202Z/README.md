# CFG publication evidence: 2026-10-15T103202Z

The [audit](../../2026-10-15T103202Z.md) owns interpretation and claim boundaries.
[Download the byte-exact archive](cfg-publication-evidence.tar.gz).

Archive SHA-256: `66e8e342b9bc1231d7f991566d38e8dfdba9e4458fc64008ed5145e465370b18`.

- `observations/final-manifest.json`: exact source/binary/input/configuration and full local validation-log hashes.
- `observations/refined-summary.json`: final six-pair complete source-to-object controls; detailed run directories retain raw per-process stdout/stderr and observations.
- `observations/refined-micro-summary.json`: final helper process comparisons including all neutral reverse-extras controls.
- `observations/census-compact.json` and `fused-census-summary.json`: baseline populations and executed final copy counts. `census/` and `fused-census/` retain native diagnostics; address-replay predictions remain explicitly labelled.
- `observations/trace-summary.json`: byte-identical token/IR/MIR/object hashes and byte sizes. Large trace streams and native binaries remain at their recorded owned local paths.
- `variants/`: independently built source variants, including rejected general sorting and the pre-fusion adverse prototype.
- `diagnostics/` and `reproduce/`: exact helper fixture, property checker, source-counter overlay and orchestration. The scripts record this session's absolute paths; point their `base` at a fresh owned workspace when reproducing. Use Git source revisions and recorded compiler flags, and verify the official SQLite archive hash before running.

The comparison entry point is repository-native `tools/performance_evidence.c`:

```sh
clang -std=c11 -Isrc -O2 -Wall -Wextra -Werror -Wno-unused-function -fwrapv -fno-strict-aliasing -funsigned-char tools/performance_evidence.c -lm -o performance-evidence
```

`reproduce/final_comparison.py` gives all frozen workload arguments. Both compiler
arms were built serially in one owned path with tests disabled. `final_micro.py`
uses native `instructions:u`, six pairs, one warmup and a 0.1 percent effect
screen. This is work/allocation evidence; elapsed time, RSS and Zen 5 speed
are not established.
