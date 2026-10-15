# Reproducing the rejected #3933 experiment

This is archival diagnostic evidence, not a supported benchmark entry point or
9700X acceptance. See the [audit](../../2026-10-13T081239Z.md) for the result and
limitations. `SHA256SUMS` pins every other file in this directory.

Use an isolated checkout at `446f742ed4ae5ed527371b0819f611f7d27a83ba`, named
`work/buster`, and put copies of `scripts/*.py` directly in `work/`. The scripts
preserve the original experiment's layout; they are not intended to run in
this evidence directory. Preserve each variant's patch and frozen executable.

1. Generate once with `./build.sh generate --cc clang --no-include-tests`, build
   Release `ide`, and copy it to `work/ide-base`. Save the compiler flags/cache.
   Run the baseline self-host check before editing source.
2. In `work/frozen`, extract `git archive
   a67057643a40c0ab99931849eb30053a2b870fcc src`; copy the configured generated
   headers to `work/frozen/generated`. Retain a new manifest: absolute shader
   paths and system-header environments differ across hosts. The captured
   experiment's manifest and package hashes are supplied here.
3. Extract the two cross-header packages named in `provenance.json` into
   `work/cross-headers`. They supply the AArch64 system-header directory used
   by `experiment.py`; no system installation is needed.
4. Save each baseline source file as `work/<basename>.base.c` for
   `register_allocator_fast`, `register_allocator_predicate`, and `codegen`.
   `instrument.py` applies the final observer; alternatively apply
   `observer-v2.patch` to the clean baseline. Build, freeze `work/ide-census`,
   and run `python3 experiment.py census` from `work/`. This compares entire
   objects and published counters against the plain binary, then attributes
   only the final selected placement. Do not time this diagnostic compiler.
5. Restore the baseline source and apply `candidate.patch`, or run
   `candidate.py` with the baseline snapshots. Build in the same configured
   path and freeze `work/ide-candidate`. Run the candidate self-host check,
   then `python3 experiment.py counts`. QUALITY is the unchanged control.
6. With all owned builds and census runs finished, run
   `taskset -c 4 python3 experiment.py timing 10`. Record ambient work and
   host controls; this desktop capture is not a dedicated-host benchmark.
7. `python3 retain_trace.py full.log selected.log` projects original lines for
   relevant values in selected placements. Reparse with
   `python3 analyze_trace.py selected.log` and compare the complete result
   with the full census. The retained four projections reproduce it exactly.
   `python3 trace_controls.py -v` exercises independent collector controls.

Use a fresh output directory for a new attempt (`BUSTER_3933_OUTPUT` selects
it). `--reuse` only reparses captures whose binary/input identities the caller
has already verified; it is not automatic provenance validation. Keep the
predeclared screen and failed attempts. Revert all production-source edits
when retaining a rejection report.
