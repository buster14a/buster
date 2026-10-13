# Provenance of audit 2026-10-12T210633Z

Raw material for [`2026-10-12T210633Z`](../../2026-10-12T210633Z.md), added
after review of PR #3891. The audit text is unchanged. Every file here was
produced in the same session as the audit, except where a line below says it
was regenerated. `SHA256SUMS` pins every file in this directory except itself.

## Host and inputs

- Linux x86_64 cloud VM, `Intel(R) Xeon(R) Processor @ 2.10GHz`, 4 vCPU.
  Clang 18.1.3 and Valgrind 3.22.0 (`--tool=callgrind --cache-sim=no
  --branch-sim=no`).
- Base: main `f0f8494491686fb8aae18626271abb2089153eba`. The compiled input
  is a worktree checkout of that revision (`src/buster/apps/ide/ide.c`, unity
  build), with the generated headers from `build/perf/generated`. Their hashes
  are in `identities.txt`.
- Build: a tests-off Release `ide` (`generate --cc clang --no-include-tests
  -- -DBUSTER_NATIVE_TARGET=x86-64-v3`). The driver was built from `build.c`
  with Clang.

## Files

| Path | What it is |
| --- | --- |
| `callgrind/*.out.gz` | The Callgrind outputs behind every Ir figure, gzip `-n -9` of the bytes Valgrind wrote. Each one records its command line and binary path. |
| `callgrind/census-tag-references-only.out.gz` | The census binary with `--collect-atstart=no --toggle-collect=census_*_tag_reference`. |
| `counters/*-pilot-v.txt` | `-v` output of each binary on the workload. **Regenerated** after the review from the same binaries; timing fields differ run to run. `census-pilot-v.txt` holds the census's per-shape `CENSUS` lines. |
| `patches/census.diff` | The census instrumentation, as a diff against the base. |
| `patches/variant6.diff` | Variant 6, as a diff against the base. |
| `patches/variant3-c_parse.diff` | Variant 3's `c_parse.c` only, as a diff against the base's. |
| `comparisons/self-cost-diff.sh` | The per-function self-cost comparison used for the audit's breakdowns. |
| `comparisons/variant*-vs-*.txt` | Its output for every variant against B (and variant 1's default path against A). They were regenerated from the gzipped outputs above. |
| `comparisons/totals.txt` | The `summary:` line of every Callgrind output. |
| `identities.txt` | Full sha256 of the base, census and six variant binaries, the build driver, every `-c` object compared, variant 3's full `c_parse.c`, and the generated headers. |
| `object-sizes.txt` | The byte sizes of those objects. |

## Replay checks run after the review

- **Census:** applying `patches/census.diff` to the base and rebuilding gives
  a binary with sha256 prefix `0fed15ba18059b87`, the census binary measured.
- **Variant 6:** applying `patches/variant6.diff` to the base and rebuilding
  gives `ef7769b8f3f5a507`, the variant 6 binary measured.
- **Variant 3:** its `c_parse.c` is retained in full. Its edits to `c.h`,
  `c_parse_internal.h`, `driver.c` and `ide.c` were not kept, and a
  reconstruction built `1d107e264faff643`, not the measured
  `7373d94c1ba16d9c`. So variant 3 is not replayable from this bundle. Its
  Callgrind output and binary hash are retained.

## Not retained

- **Variants 1, 2, 4 and 5:** their sources were overwritten as each next
  variant was built. Only their binary hashes, Callgrind outputs and
  regenerated counters remain. Their Ir figures can be checked against the
  Callgrind outputs, but the code behind them cannot be replayed.
- **The binaries and objects themselves:** they total about 0.7 GB and are
  not committed. Only their hashes are kept.
- **The object identity checks:** these compared the full sha256 of each
  object in `identities.txt`. The comparison itself was not saved separately.
