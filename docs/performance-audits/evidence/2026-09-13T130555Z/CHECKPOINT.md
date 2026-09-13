# Issue 564 measurement checkpoint

## Frozen experiment

- Main observed before work: commit `bf84b0202a4d3268587d015e6c19e5ae1f785e4a`, tree `11ad06d91ad7f99dc0b2cc6e091814422b6d17c5`.
- Corrected measurement compiler source: commit `b564aae8ededa5f7a7866987b5dd30ca0f9cff05`, tree `719b810e86e2f8bdb73de425b0aa1237b88e6ce9`.
- Final diagnostic-parity workflow commit: `46f96daf2725f9dccc06c16b2d3c60e484d045a4`; it changes no compiler source or compiled workload bytes from `b564aae`.
- Producer: repository Release `ide` built by Clang 21.1.8. `final-znver3/CMakeCache.txt` and `compile_commands.json.gz` freeze the ordinary build; the `census-` files freeze the diagnostic build.
- Workloads: the repository unity self-compile and `final-znver3/small.c`. `inputs.sha256`, `generated.sha256`, and the source metrics freeze bytes and dependencies.
- Target: explicit Buster `-march=znver3` for ordinary, census, and Callgrind workload compiles. The host exposes AVX2, not AVX-512.
- Profiler: Valgrind/Callgrind 3.26.0, `--cache-sim=no --branch-sim=no`; only the `Ir` event is collected.

## Attribution definitions

- Work counters are exact successful-path populations from the existing `BUSTER_BENCH_ALLOCATIONS` calling-thread accumulator. They are not costs.
- Exclusive cost is Callgrind Ir mapped directly to source lines in a check family's live range.
- Direct-child cost is Callgrind Ir attributed to callees at call sites in that range.
- A source subtotal is exclusive plus direct-child cost. The primary source-family rows are disjoint.
- Drill-down rows in `attribution.json` overlap their parents, and conversion and provenance drill-downs overlap each other. They must not be summed.
- Line-zero, out-of-range inlined-helper, and other-file cost remains unattributed.
- The ownership and semantic chain visits cover equal instruction populations but prove different invariants; equality is not a redundancy claim.

## Instrumentation scope

The added counters extend `IR_CONSTRUCTION_COUNTERS`; they do not introduce a
new reporter or storage owner. `IR_CONSTRUCTION_RECORD` compiles away without
evaluating its arguments when `BUSTER_BENCH_ALLOCATIONS=0`. With the diagnostic
enabled, the pre-existing TLS accumulator owns storage. The instrument records
module/check populations plus the four canonical-preparation validation call
sites and adjacent function visits. It does not disable, extract, or alter a
validation check.

## Raw evidence locations

- `final-znver3/`: corrected, explicitly frozen target; authoritative for the disposition.
- `diagnostic-parity/`: successful invalid-input status/stdout/stderr parity on commit `46f96d`, plus the retained EVEX decoder failure from the following profile repeat.
- `preliminary-haswell/`: retained incomplete run where Valgrind's masked CPUID selected `haswell`; not used for a native or Zen 5 claim.
- `summarize_callgrind.py`: parser for optimized source-line self/direct-child attribution.
- `RAW-SHA256SUMS`: decompressed SHA-256 identities for the deterministically
  gzipped unity Callgrind streams.
- GitHub Actions corrected profile: run `34758731305`, artifact `10317209392`, artifact digest `sha256:2cc546c21eb53c490da8debd6a0c85dabd0916bad0f537d725a6d13414ca6541`.
- GitHub Actions diagnostic parity / failed EVEX repeat: run `34759148980`, artifact `10318761068`, artifact digest `sha256:9fc0b9e99959a0111639cc659d81855ffa9cf3dd23968409ef7152f99f55b181`.
- GitHub Actions preliminary profile: run `34758100399`, artifact `10317373646`, artifact digest `sha256:61e40eea9404c78e3b6512d7c40098c8be4979d2e7b93b8a07b7cdb370e88687`.

The source archives and produced executables are not duplicated in git because
they are 113 MB and 39 MB respectively. Their SHA-256 identities are retained;
the source archive is reproducible with `git archive --format=tar <commit>`.
