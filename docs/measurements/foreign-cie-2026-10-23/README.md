# Foreign terminal CIE pooling measurements, 2026-10-23

These are fresh whole-link measurements of original static-executable fixtures
on one lane of the shared Ryzen 7 9700X. Pooling remains opt-in. The measurements
cover the terminal CIE adapter, including its complete-link copy; they do not
isolate CIE comparison, SIMD, or the benefit of removed records. Issue #86
remains open.

## Source and validation

- Source commit: `ddfb351b247259f05898a31ff54c6477bc5e5774`.
- [Frozen source manifest](foreign86-cie-v1-frozen-source.sha256), SHA-256:
  `d93b0043c68c571df3e25a670f18ccbd722820124995c294a69f37c0931f64f4`.
- Compiler SHA-256:
  `12b654dadfd73779055aa5d5e4086813c11b9b58584d50d3524dca90f2bdb0f3`.
- Benchmark source SHA-256:
  `250a8dc9f9abb1bd1c2dfee3416f5d9256e5111f4b9249a00430059db04e522a`.
- Remote source directory:
  `/home/david/buster-fast-bench/codex-linker-foreign86-cie-v1`.

The compiler sources and build script at this commit match the frozen
manifest. This isolated source uses the newer writer, whose output file gap
adds 4,096 bytes relative to the earlier composed dedup-v2 snapshot. The
pooling core is unchanged, but the writer and output layout differ. No values
from the older `foreign86-cie-whole-*` campaign enter these tables.

The isolated [build](foreign86-cie-v1-build.log),
[foreign cross-check](foreign86-cie-v1-test.log),
[call-graph check](foreign86-cie-v1-callgraph.log), and all three benchmark
processes passed. The cross-check reports 812 terminal CIE checks, 136
COMDAT/GC/CIE composition checks and 72 C++ unwind runtime checks, all with
zero failures; every other reported test group also passed. The call graph
has 38 translation units, 20,486 functions, 113,686 calls, zero unapproved
cycles and three allowed cycles.

Each benchmark process reports 1,118 pipeline validation checks with zero
failures: 280 for each duplicate-CIE size and 279 for each unique-CIE size.
Untimed replay checks original ELF ownership, record and endpoint maps,
relocations, serialized FDE addresses/ranges and the fixed CIE/FDE templates.
Mutation controls reject duplicate or wrong-offset mappings. Every sampled
output runs and must match its pooling state's complete reference bytes
outside timing. Unique-CIE outputs must also match across pooling states.
This synthetic timing fixture has no COMDAT selection, GC, personality/LSDA,
dynamic runtime, corpus or multicore workload; the separate C++ correctness
gate does not broaden its performance scope.

## Commands and timing boundary

Each command ran separately through
`/home/david/buster-fast-bench/exclusive.sh`, with
`EXCLUSIVE_OWNER=codex-linker-foreign86`, a per-run `EXCLUSIVE_TITLE`, and
`EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86`. CI retained
priority. Build and correctness commands in the frozen source directory were
`./build.sh`, `./build/compiler test-foreign --cross-check --keep`, and
`./build.sh check-call-graph`.

```text
taskset -c 2 ./build/compiler benchmark-foreign-unwind-dedup --counts 8,1024 --runs 21 --keep
taskset -c 2 ./build/compiler benchmark-foreign-unwind-dedup --counts 8,1024 --runs 21 --order dedup-vector,no-dedup-vector,dedup-exact,no-dedup-exact --keep
taskset -c 2 ./build/compiler benchmark-foreign-unwind-dedup --counts 8,1024 --runs 21 --schedule blocked --keep
```

The complete logs are [run 1](foreign86-cie-isolated-20261023-run1.log),
[run 2](foreign86-cie-isolated-20261023-run2.log), and
[run 3](foreign86-cie-isolated-20261023-run3.log). The first two rotate variant
order between samples, starting in opposite orders; the third blocks samples
by variant. Each has one untimed validation replay and one warm-up per variant,
then 21 measured links. No scalar conditioning was requested.

Every measured link starts before reservation of its link/name arenas and
includes input mapping and resolution, conversion, optional pooling, writer
close, input unmapping and arena release. Fixture and command generation,
output unlink, conditioning and validation are outside the clock. Files are
generated once per process and reused; input files and filesystem caches are
warm. Exact/vector select the corresponding resolution, pooling and writer
paths, so their comparison does not isolate the pooling kernel.

Retained fixture directories on the measurement host are
`/tmp/buster-foreign-unwind-dedup-MrjpJd`,
`/tmp/buster-foreign-unwind-dedup-ZrVMtu`, and
`/tmp/buster-foreign-unwind-dedup-3XeBMe`. Their host input identity receipts
are pending. No digest from the older campaign is substituted for them.

## Whole-link process medians

Each cell is the median of the three processes' integer nanosecond medians,
converted to microseconds, with the minimum–maximum process-median range.
The percentage is `(pooling / no pooling - 1) * 100`, calculated before
rounding. These ranges describe observed spread, not confidence intervals;
the individual sample minima/maxima remain in the logs. There are N leaf
objects plus one entry object, and every leaf remains live in both fixtures.

| CIE fixture | Leaves | Path | No pooling, us (range) | Pooling, us (range) | Median change |
| --- | ---: | --- | ---: | ---: | ---: |
| Duplicate | 8 | exact | 56.777 (55.875–57.388) | 55.243 (54.732–55.584) | -2.70% |
| Duplicate | 8 | vector | 57.228 (55.985–57.297) | 54.883 (54.562–55.214) | -4.10% |
| Duplicate | 1,024 | exact | 3437.866 (3433.126–3493.404) | 2814.633 (2790.835–2821.883) | -18.13% |
| Duplicate | 1,024 | vector | 3628.774 (3582.802–3653.951) | 2934.167 (2917.953–2999.065) | -19.14% |
| Unique | 8 | exact | 57.147 (57.097–57.618) | 54.723 (54.252–55.012) | -4.24% |
| Unique | 8 | vector | 56.466 (55.895–57.538) | 54.492 (54.121–55.213) | -3.50% |
| Unique | 1,024 | exact | 3599.609 (3579.226–3600.230) | 3000.912 (2951.816–3001.780) | -16.63% |
| Unique | 1,024 | vector | 3656.480 (3622.762–3674.179) | 3016.235 (3009.564–3021.060) | -17.51% |

The 1,024-leaf duplicate case removes 1,023 CIEs and EH atoms, or 32,736
bytes. Its EH data shrinks from 57,344 to 24,608 bytes, and its executable
from 95,136 to 62,400 bytes. The eight-leaf duplicate case removes seven
CIEs/atoms and 224 bytes, shrinking the executable from 5,728 to 5,504 bytes.
The unique cases remove nothing and preserve their entire output bytes.
All recorded variants report zero unsupported fallbacks.

## Memory and interpretation

These retained arena lengths are identical across the three processes. Each
pair gives no pooling / pooling bytes; they exclude input mappings and
transient scratch, and are not peak RSS or reserved address space.

| CIE fixture | Leaves | Exact retained arena bytes | Vector retained arena bytes |
| --- | ---: | ---: | ---: |
| Duplicate | 8 | 17,701 / 24,852 | 17,989 / 25,140 |
| Duplicate | 1,024 | 1,846,895 / 2,539,096 | 1,879,695 / 2,571,896 |
| Unique | 8 | 17,677 / 25,436 | 17,965 / 25,724 |
| Unique | 1,024 | 1,843,823 / 2,607,672 | 1,876,623 / 2,640,472 |

Pooling shortens the measured total beyond the observed process-median spread
for these large inputs, while retained arena bytes grow. The unique-CIE
control also gets faster despite removing nothing: the transactional adapter
copies the link into new storage, changing placement and subsequent access.
These results therefore establish neither a CIE-only nor a SIMD-only cause.
Page faults, cache attribution and peak RSS were not measured. The eight-leaf
changes are only about 1.5–2.4 microseconds, with overlapping individual-sample
ranges; they are limited observations, not a general small-input guarantee.
Phase medians must not be summed. TSC ticks are reference ticks, not measured
core cycles. Performance qualification is still pending: these three processes
all used `--after-scalar 0`. Bounded repeated processes with
`--after-scalar 100` and review of the exact build's emitted assembly remain
before final SIMD-state acceptance. Broader corpus and scaling acceptance
remain open, and pooling stays opt-in.
