# Refinement budget qualification, 2026-10-23

This draft experiment addresses the remaining #116 budget: the actual
`benchmark-preprocessor` refine phase for raw SQLite and its system headers,
including any cost moved into adjacent phases. It makes no performance claim
until repeated 9700X results are recorded. Production refinement remains the
baseline while the candidates are compared.

The baseline refinement source is unchanged between `c62cd840` and
`5d78cc2b1e6b608c6ce04b6147ef0ba7f3f86d4f`. The latter is the integration base
for the draft CI comparison. Every candidate patch applies to that baseline,
and every executable includes the same current preprocessor benchmark.

| Candidate | Change from the baseline |
| --- | --- |
| base | Unchanged implementation |
| compress | Extract the first matching ID from the loaded bucket with a register compress |
| batch4 | Compress plus four probe results packed into one vector store |
| fused4 | Compress plus probing four identifiers immediately after their vector hash |
| bitmap | Compress plus reuse of the existing backslash-chunk bitmap |
| short16 | Bitmap plus first-16-byte hashing, with complete rehashing for longer identifiers |
| outline | Baseline probe with the hash batch kept out of line |
| combined | Fused4, bitmap reuse, and four-result vector stores |

The first screen's candidates preserve the existing hash values, normalized spellings, first-seen IDs,
leading-space bits and physical literal spellings. Fused4 and combined probe
the next batch before the current batch's deferred inserts. Misses are retried
in token order, and table growth invalidates and recomputes the prefetched
batch. The new regression cases force growth at eight batch boundaries.

The compress idea was also tried and discarded in the earlier cloud session
recorded in [#116's handoff](https://github.com/buster14a/buster/issues/116#issuecomment-6353068245).
This experiment was independently written against the same first-party
implementation and must earn its place with target-host measurements. No
external implementation is imported. The small C entry points include the
repository's benchmark and tests, with their existing component notices.
SQLite and installed system headers are runtime inputs and are not vendored.

## Inputs and procedure

| Runtime input | SHA-256 |
| --- | --- |
| SQLite 3.53.4, source ID `bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` | `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189` |
| SQLite 3.53.2, source ID `d6e03d8c777cfa2d35e3b60d8ec3e0187f3e9f99d8e2ee9cac695fd6fcdf1a24` | `0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d` |

The draft workflow uses frozen copies in the host's shared directory and checks
both hashes before building. It compiles each overlay with the normal flags,
runs the scalar/oracle correctness suite, and uses the native `ab` command to
rotate all eight executables across three processes per input. Each process
runs the existing benchmark's 21 passes from fresh preprocessor state. CPU 2
is fixed; the runner job hooks hold the shared host lock. Manual reproduction
must add `--lock /home/david/buster-fast-bench/exclusive.sh` to `ab` and use
`exclusive.sh` for each build, test and profile run separately.

The extra workflow step is temporary and restricted to this draft branch. It
does not change the required tests, CI priority or timeout. It will be removed
before the final implementation is submitted for merge. The candidates are
experiments, not production alternatives or runtime dispatch paths.

## Correctness so far

Local diagnostic validation passed the scalar and independent-oracle suite for
all candidates: guarded probe patterns, structured hashes, 300,000 interned
spellings, eight growth boundaries, all 64 alignments, splice/UCN cases,
random inputs and guarded page edges. The combined candidate also passed
ASan/UBSan with seed 42 and repository sources plus frozen SQLite. These local
results do not establish Zen 5 performance or replace target qualification.

An additional baseline run with seed 116 reports one unchanged hash in 14,861
aligned two-bit-flip cases, while scalar/vector hashes and all identifier,
whitespace and table checks agree. This pre-existing hash-quality assertion is
being isolated separately; none of these candidates changes the hash function.
The draft comparison uses the validated seed 42 identically for every variant.
The seed-116 failure remains recorded and is not a passing validation result.

## First 9700X screen

[Run 39943511804](https://github.com/buster14a/buster/actions/runs/39943511804)
passed the required job on draft head `761eec22` merged with `42630114`.
[The raw comparison](screen-first-9700x.txt) contains 72 successful processes,
each with 21 passes; [the summary](screen-first-9700x.tsv) retains the ranges.
On SQLite 3.53.4 the baseline refine phase was 1.693–1.712 ms, median 1.696 ms.
Compress and batch4 reached medians 1.674 and 1.673 ms; their whole-run ranges
overlapped the baseline. Fused4 and combined were slower, at 2.039 and 2.091 ms.
None meets the 1.5 ms acceptance requirement. No candidate has been selected.

## Second screen

[The hash regression](hash-regression.md) isolates the seed-116 failure to
discarding the high half of the folded AES state. The proposed final mixing
round fixes all four deterministic pairs and the 128-seed diagnostic sweep.
It changes internal table hashes, preserving scalar/vector equality, normalized
spellings, IDs and layouts. Its cost is part of the next measured comparison.

The second screen compares the original hash/refinement (`base`), the hash fix
alone (`finalmix`), and the fix with compress, batch4, ahead2 and ahead4. The
last two keep hashing and probing in separate stages, but prefetch two or four
batches ahead through a bounded ring. Growth invalidates bucket offsets before
probing, and inserts retain token order. Both passed the full seed-116 suite
over repository sources, SQLite and the Zig libc corpus locally.

The original hash is only a timing control: its known failure remains recorded,
and it is excluded from the new fixed-fold tests. Every proposed implementation
runs those tests with seed 116 on the 9700X. The screen rotates three processes
per input, and also measures the refinement kernel back to back and after
100 ms of scalar work. Separate cycle profiles are diagnostic and excluded from
the comparison. The user confirmed that the 1.5 ms target remains required.
