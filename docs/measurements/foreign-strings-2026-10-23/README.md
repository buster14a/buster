# Foreign string consumer measurements, October 23, 2026

The consumer search change is retained: all twelve duplicate-and-pool cases
improved complete link time beyond the observed three-process ranges, with
no control slowdown having disjoint ranges. Pooling remains opt-in and still
costs more than omitting it in these workloads. This is bounded evidence for
[#86](https://github.com/buster14a/buster/issues/86), not complete foreign-linker
acceptance.

The [guide](../../foreign-string-merging.md) describes the supported expressions.
The [full report](raw/report.txt) contains every timing boundary, resource
summary and qualification. Original receipts and machine-readable tables are
preserved under [raw/](raw/).

## Frozen source and current composition

| Role | Revision | Archive SHA256 | Compiler SHA256 |
| --- | --- | --- | --- |
| Baseline | `e74b6727689103a0891140fc93bbc6929f7d304b` | `5955acd3e00e97ea2edff8122ebebb5430b216091ef139e98d80c0dfb9e6b9c8` | `a6a498a755052934b94adebc335c03e5dcd7fc707d8309f7b7d11c1e9e385237` |
| Candidate | `aa2e3257cf2105c39e626a3c457989aea05c16be` | `2f9220204f6aa0f21c59ecf5fbce6a87808993f7e1f479d175b8f89285cf3897` | `a19cafca33a82f842b04f5cc0e44464d086a2c646afbe296e6e0a04ae8bd4c80` |

Only `foreign_strings_link.c` differs between these measured sources. The
candidate removes a second symbol-entry search, skips searches for named
zero-addend fixups already proved by symbol translation, and uses one
source-entry search plus relative bounds for nonzero named addends. Benchmark
sources are identical.

PR328's local composition `41196d87ff8f2a9b2b86c0c19b489dd49729da17` includes
later main changes and the candidate branch. Its consumer bytes match the
measured candidate; the kernel and string tests also match. Its benchmark
additionally includes an untimed no-saving output-comparison improvement
from `8a1e6d2`/`5fbd15c`. [source-blobs.txt](source-blobs.txt) records these
source identities. This composed head was **not** timed in the campaign.
The measurements do not stand in for its final-head CI or integration checks.

The frozen benchmarks only compared unique outputs within each merge state.
A separate external check established all-four-variant identity after timing;
that stronger check is not attributed to the old benchmark code.

## Method

All twelve fresh processes ran on the approved Ryzen 7 9700X, pinned to CPU 2,
each through its own shared lock with ordinary CI priority. The host used
Clang 23.1.1 and the recorded `-O3 -march=native` build flags. CPU 2's governor
was `performance`, boost was enabled, and CPU 10 was offline at both captures.
No settings were changed. Generated inputs and outputs were on `/tmp`
(`tmpfs`); the two build trees were on `btrfs`.

Each process measured 64, 4,096 and 131,072 literals, with eight repeated
values or all unique values, in exact/vector and pooled/unpooled modes.
Every path had one warmup and eleven timed samples. Three process pairs ran
at each conditioning state: 0 ms and 100 ms. Pair order alternated, and the
second repetition reversed variant order. See the exact [protocol](raw/protocol.txt),
[commands](raw/01-baseline-0ms-r1.command), and [process census](raw/processes.csv).

The common invocation, after acquiring the shared lock, was:

```sh
tls85-measure taskset -c 2 COMPILER benchmark-foreign-strings \
    --counts 64,4096,131072 --runs 11 --keep --directory /tmp \
    --order ORDER --after-scalar MS
```

The [original C helper](tls85-measure.c) was built separately under the shared
lock with `cc -O2 -Wall -Wextra tls85-measure.c -o tls85-measure`. Its source
SHA256 is `ec322944bf1e841596d43142f97d9807968e8a92874bc59be92093b19027dc3a`;
the reused executable SHA256 is
`9007ad8a6398b10e0ee2e6a5a8b48373b3e115f2ef41a4a65117bf65b5dd5f07`.
It reports `wait4` peak RSS and user/system time for the **complete process**,
including generation, warmups, conditioning and validation.

The link timer includes per-link name-arena reservation, fresh resolver maps,
resolution, conversion, optional pooling, symbol entry lookup/writer wrapper,
mapping/name-arena release and output-arena reset. Input generation,
previous-output unlink, conditioning, execution and whole-image validation
are outside. Inputs and the page cache are warm; fresh mappings do not mean
cold inputs. Phase medians are independent and must not be summed.
Throughput uses exact printed wall nanoseconds; TSC ticks are reference ticks,
not core cycles.

The acceptance rule was written [before measurement](raw/premeasurement-audit.txt):
repeat whole-link improvements beyond observed process ranges, and block
retention on any negative-control slowdown with disjoint ranges. These
ranges describe three process medians, not statistical confidence intervals.

## Complete link results

Times are microseconds: median of three process medians [minimum, maximum].
Each process median covers eleven links. All twelve ranges below are
disjoint in favor of the candidate.

| Conditioning ms | Literals | Path | Baseline µs [min, max] | Candidate µs [min, max] | Reduction |
| ---: | ---: | --- | ---: | ---: | ---: |
| 0 | 64 | exact | 49.603 [48.761, 50.123] | 47.800 [47.559, 47.849] | 3.63% |
| 0 | 64 | vector | 34.284 [34.144, 35.166] | 32.321 [31.979, 33.853] | 5.73% |
| 0 | 4,096 | exact | 1619.710 [1609.090, 1624.129] | 1555.751 [1555.650, 1569.276] | 3.95% |
| 0 | 4,096 | vector | 571.880 [564.035, 584.684] | 503.402 [499.324, 529.060] | 11.97% |
| 0 | 131,072 | exact | 61064.394 [60135.456, 61181.733] | 59775.433 [58680.905, 59891.769] | 2.11% |
| 0 | 131,072 | vector | 27971.221 [27052.010, 28147.620] | 26655.919 [26620.744, 26818.112] | 4.70% |
| 100 | 64 | exact | 52.529 [51.646, 74.008] | 49.292 [48.330, 49.673] | 6.16% |
| 100 | 64 | vector | 36.168 [34.635, 37.941] | 33.553 [33.393, 33.874] | 7.23% |
| 100 | 4,096 | exact | 1645.338 [1644.416, 1652.031] | 1565.578 [1564.356, 1566.801] | 4.85% |
| 100 | 4,096 | vector | 620.661 [619.580, 631.501] | 545.541 [541.152, 549.057] | 12.10% |
| 100 | 131,072 | exact | 61170.864 [61034.749, 61264.237] | 58936.033 [58823.442, 59051.269] | 3.65% |
| 100 | 131,072 | vector | 27990.958 [27391.977, 28221.068] | 26652.734 [26609.943, 26668.283] | 4.78% |

Across [all 48 comparisons](raw/comparisons.csv), fourteen improve with
disjoint ranges and 34 overlap; none regresses with disjoint ranges.
Control median changes span −2.279% to +0.920%. Two controls improve
disjointly: conditioned unique-64 exact pooling and duplicate-4,096 vector
no-pooling. This supports retention without proving control equivalence or
assigning every total-time change solely to the removed searches.

The largest input contains 513 original ET_REL units and 19,473,440 bytes.
Duplicate literal storage shrinks from 4,194,304 bytes to 256, and the executable
from 5,243,864 bytes to 1,049,816. Candidate vector input throughput is
0.731 GB/s in both states, versus 0.696 GB/s for the baseline. Its merge
phase median falls from 7,652.0 to 6,217.8 µs without conditioning and
7,734.5 to 6,198.8 µs after conditioning. Other phase variation also contributes
to total time; [phases.csv](raw/phases.csv) preserves it.

## Cost of choosing pooling

Candidate-versus-baseline gains do not make pooling free. All 48
[pooled-versus-unpooled median comparisons](raw/pooling.csv) cost more with
pooling; 45 ranges are disjoint and three overlap. On the largest duplicate
case, candidate vector pooling takes 26.656 ms versus 21.037 ms without
pooling (+26.71%) at 0 ms, and 26.653 ms versus 21.236 ms (+25.51%) at
100 ms. Exact pooling costs +123.88% and +120.58%, respectively.
Unique inputs save no space and remain unchanged. The pass therefore stays
an explicit consumer choice; this result does not enable it in the default
foreign driver.

Retained arena lengths are unchanged baseline/candidate in every comparison.
For the largest duplicate/vector case, pooling retains 76,499,582 bytes
versus 74,922,337 without pooling, measured before cleanup. These are used
lengths, not reserved capacity or peak RSS. Whole-process peak RSS ranges
overlap: 134,248–134,584 versus 134,384–134,516 KiB at 0 ms, and
136,332–136,644 versus 136,416–136,788 KiB at 100 ms. No memory gain is
established. Conditioned process CPU time includes at least 28.8 seconds
of conditioning, which the per-link timer excludes.

## Validation and assembly

Both frozen binaries passed full `test-foreign --cross-check`; all twelve
timed processes exited zero and passed 14,112 validation checks. Every
process retained 1,064 ET_REL inputs and 24 outputs. The [external gate](raw/identity.command)
verified corresponding outputs byte-for-byte across all processes/binaries,
unique outputs across all four variants, and duplicate exact/vector identity.
All input hash manifests match; archive, compiler and helper hashes were
unchanged afterward. The [original manifests](raw/retained-manifests.txt)
and [normalized hashes](raw/input-output-manifest-hashes.txt) are preserved.

The changed `foreign_strings_apply` shrinks from 3,072 to 2,916 bytes; both
retain six pushes and 88 local stack bytes. This is removed search work,
not spill elimination. The scalar conditioning hot loop is integer code,
but clock bookkeeping contains XMM/VEX instructions; neither an AVX-free
condition nor a power-state mechanism is established.

The benchmark's selected writer clone contains 21,445 instructions/104,829
bytes at identical addresses in both binaries. [Complete disassembly comparison](raw/writer-disassembly.diff)
finds exactly four differing bytes, each a RIP-relative data displacement
whose named target moves −0x90. All other instruction bytes and branch/call
targets agree. Data addresses/cache alignment differ; the pointed-to contents
were not separately checked by this inspection. The first read-only hash
wrapper had a documented local receipt error after remote completion;
the corrected disassembly wrapper exited zero. Measurements and identity
receipts were unaffected.

An independent reviewer reconciled all tables and the complete writer
difference; see [the review record](raw/independent-review.txt). These synthetic
named, zero-addend data-pointer fixtures do not measure nonzero-addend or
section-relative performance, real-corpus gains, parallel interning, a complete
linker CLI, whole compilation or cold storage.

## Evidence and provenance

All 99 files from the completed evidence directory, including its historical
manifest, are copied byte-for-byte to `raw/`. Their original paths, commands,
logs, receipt error and unsuccessful preliminary symbol lookups remain intact;
the successful callsite/disassembly files establish the active writer clone.
The historical `raw/SHA256SUMS` names the original workspace paths. Use this
directory's [SHA256SUMS](SHA256SUMS) from this directory to verify the packaged
files: `sha256sum -c SHA256SUMS`. It covers every file here except itself.
The local `.gitattributes` preserves raw output whitespace, following the
repository's other frozen measurement records; prose retains normal checks.

The 288 rows in [samples.csv](raw/samples.csv) are per-process/path medians,
not the eleven underlying observations, which the benchmark does not print.
[analysis.json](raw/analysis.json) retains the unrounded derived values.
The generated object files and executables remain on the approved host;
their complete hashes are retained here, rather than binary artifacts.

The helper, benchmark inputs and measurement documentation are original
project material under Apache-2.0 WITH LLVM-exception. Inputs come from the
repository's original `foreign_strings_benchmark_generate` at the frozen
revisions. Clang, GNU binutils and lld run externally as compiler, inspection
tools and semantic oracles; no implementation or external fixture corpus
from those tools is imported. Existing compiler components retain their
recorded terms in the repository notices.
