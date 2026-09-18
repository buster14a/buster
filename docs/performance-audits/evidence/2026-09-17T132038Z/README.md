# Issue #566 screen evidence

Audit: `2026-09-17T132038Z`. Workflow run `35226037480`, job
`105218243891`, artifact `10498099770`. The measured product source is `main`
commit `5e3d839edaa9407f68e3a981fa5cde0ba3beb409`; the measurement commit differs
from it only by the temporary workflow used to collect the artifact.

The exact GDB census contains 4,896 unity calls summing to 1,337,343
instructions, and one four-instruction small-input call. Both sums match the
nonsampling `ir_construction.validation_ownership_instructions` counters, and
`validation_ownership_bytes_cleared == instructions * sizeof(IrBlockId)`.

The ordinary and diagnostic producers emitted byte-identical unity and small
outputs. Raw metrics differ only in `allocation.arena_bytes` by +144 bytes
under the diagnostic run; all other metric rows compare byte-for-byte after
removing that aggregate row. No timing result is taken from either producer.

The retained evidence is:

- `screen.json`: machine-readable summary, arithmetic and artifact identities.
- `populations.txt`: lossless exact histograms plus SHA-256 hashes of the
  ordered raw breakpoint streams retained in the Actions artifact.
- `unity-metrics.txt` and `small-metrics.txt`: complete ordinary source-metric
  reports, exact patches that reconstruct the diagnostic reports, and equal
  normalized hashes after removing `allocation.arena_bytes`.
- `provenance-build.txt`: environment, commands, producer/input/output hashes,
  output sizes, the diagnostic symbol and the temporary noinline patch.
- `provenance-logs.txt`: GDB census logs, ordinary unity stdout and exact small
  input. The ordinary stderr and small ordinary stdout/stderr captures were empty.

The 40 MB emitted binaries are represented by size and SHA-256 rather than
committed; the artifact retains the originals for 90 days. The diagnostic
metric reports are exactly reconstructable from the committed ordinary reports
and diffs.
