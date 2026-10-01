# Delimiter producer-fact experiment

Select producer-owned delimiter pairs as a bounded work-elimination slice.
[Decision](DECISION.md), [new audit](../../performance-audits/2026-10-01T204938Z.md),
[owner #2183](https://github.com/buster14a/buster/issues/2183) and
[candidate PR #2191](https://github.com/buster14a/buster/pull/2191).

Inspection baseline: `c5a073139691e9111986c4fca6f6b03d2a6dfcf1`, tree
`68adbe94cf6bd2c00671c4e413552d54791fd1af`.
Production implementation: `0df37ef91b91c8c444bfbf8002aee93b27c72f99`.
Strict package/candidate capture: `187b463a305ddd80ac26a6e855916b741e677823`.
Subsequent commits change evidence tooling/documentation only. Codex root is the
sole writer on `codex/2183-publish-delimiter-pairs`; scouts/reviewers are read-only.

## Results and limits

The strict cloud run removes 5,705 inverse-map allocations, 2,042,050 token
visits and 8,168,200 logical clear bytes for frozen Buster unity input. It adds
450,797 four-byte pair-publication stores in existing storage. Outputs and
diagnostics match. Both own-source self-host fixed points, focused frontend
tests, sanitizer tests and the mode matrix pass; the matrix comprises four
native legs and twenty link-only legs.

Removed-region timing is only 0.111–0.119% of instrumented unity compilation.
Plain unity medians are mixed across captures (first +0.32% wall, strict -1.28%).
Strict updates/labels medians regress +1.10%/+1.73%. This is a negative result
for a current 2x or step-change claim. Shared-cloud timing is descriptive.
The identical small-input RSS floor is unresolved; it cannot prove memory
neutrality. Qualified-host performance, resource, code-size/runtime and remaining
native-platform acceptance are explicitly pending. No laptop or benchpress/9700X
execution was used or authorized.

## Reproduce in an isolated cloud job

Use a fresh disposable GitHub-hosted Ubuntu 26.04 checkout with full git history,
Clang 21, CMake and Ninja. Run as a 45-minute-bounded job with two build workers.
The package uses Python's standard library for orchestration only; compiler
implementation and dependencies remain C-only and unchanged.

```sh
export BUSTER_TEST_JOBS=2
export CMAKE_BUILD_PARALLEL_LEVEL=2
python3 docs/research/delimiter-facts/run.py \
  --baseline c5a073139691e9111986c4fca6f6b03d2a6dfcf1 \
  --candidate 187b463a305ddd80ac26a6e855916b741e677823 \
  --out "$RUNNER_TEMP/delimiter-evidence"
```

The script enforces GitHub Actions execution and a clean checkout. It temporarily
checks out both exact commits in the same path and restores candidate files.
Use the immutable
[capture workflow](https://github.com/buster14a/buster/blob/187b463a305ddd80ac26a6e855916b741e677823/.github/workflows/delimiter-facts-experiment.yml)
as the job recipe; the temporary workflows are removed from the final PR.

`run.py` freezes baseline source, headers, generated files and synthetic
fixtures; records actual commands, host/compiler identity, binary hashes,
source hashes and overlay provenance; and uses bounded subprocesses.
`instrument.py` modifies disposable census builds only. Normal timing binaries
contain none of these observers. Census requires all three inverse populations
to become zero and reverse stores to equal pair count; missing fields fail.

Quality checks use five frozen inputs, two frontend forms and -g object-byte/
diagnostic equality, with instrumented/plain equality. Two invalid controls must
produce equal nonzero status/diagnostics and no object. Timings use -g0, two warmup
pairs and six measured pairs, alternating AB/BA. Every timed output is hashed.
All commands, intermediate checkpoints and failures are retained. The report
does not infer an end-to-end speedup from region timing.

## Evidence

| Capture | Exact candidate | Status / role |
|---|---|---|
| [Baseline before edits](https://github.com/buster14a/buster/actions/runs/36919857990) | `8e78180790679855bfd0f0c01a6e99ff23084ea5` | Unchanged compiler baseline fixed point |
| [Preliminary](https://github.com/buster14a/buster/actions/runs/36921566974) | `0df37ef91b91c8c444bfbf8002aee93b27c72f99` | Correctness passed; census/parity/checkpoint gaps preclude acceptance |
| [Strict](https://github.com/buster14a/buster/actions/runs/36923086652) | `187b463a305ddd80ac26a6e855916b741e677823` | All explicit falsification checks passed; descriptive diagnostics |
| [Archive reader](https://github.com/buster14a/buster/actions/runs/36923953503) | `fef879bf2e1c4f4e92ad15032c5bd1df98951705` | ZIP digest/size/CRC verification; audit path minted using tools/new_audit.py |

[evidence.json](evidence.json) and [preliminary.json](preliminary.json) preserve
raw paired timing rows, selected raw delimiter counters, quality controls,
actual command/status/timeout records, hashes, host and build details, source
manifest and correctness summaries. Unrelated census fields and verbose logs
are omitted from these compact exports. Full archives contain those fields,
logs, frozen inputs, binaries, build flags and generated closure; they expire
after 30 days.

- [Strict archive](https://github.com/buster14a/buster/actions/runs/36923086652/artifacts/11192529455):
  53,860,952 bytes; SHA-256
  `dac3c8d28fe87ce69bc21757c672140e3821bc349d6dcdfe4bc36faae2f6122c`.
- [Preliminary archive](https://github.com/buster14a/buster/actions/runs/36921566974/artifacts/11193195299):
  53,657,360 bytes; SHA-256
  `39293f252b08063061b62c3d6533fe4ac0f9e627fc073cdccce6430b1bebbc2e`.

`report.py` is an optional cloud-only archive reader. Give its cloud job an
actions-read token as GH_TOKEN. It verifies ZIP digest/size/CRC, frozen-file and
recorded binary hashes without executing archived programs, and prints JSON
records. It permits the explicitly missing preliminary driver hash; the strict
capture requires it. [Final reader run](https://github.com/buster14a/buster/actions/runs/36925504068)
verified 411 strict frozen files, recorded binaries and a clean git diff --check.
Exact license sources and component distinctions appear
in the audit; no researched external implementation was imported.
