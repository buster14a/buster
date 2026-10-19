# C lexer: Zen 5 optimization

Measured on 2026-10-19 on the Ryzen 7 9700X (`benchpress`), using Clang 23.1.1 and
`build.sh`'s `-O3 -march=native`. Each build, test, benchmark and profile took the
shared `~/buster-fast-bench/exclusive.sh` lock separately. Benchmark processes
were pinned to CPU 2, with the performance governor; CPU 10 was offline and CPU
2's reported online sibling list was just `2`.

## Results

Both executables read identical, frozen input trees. The baseline was commit
`015df42f1f8eb3d48d2ad75ecd9b0daed15d07fc`. Each command performs 21 interleaved
vector/scalar passes after warming up. The table reports the median of three
command runs per executable, alternating baseline/candidate order between rounds.
All corpus comparisons against the scalar lexer passed.

| Corpus | Files / bytes | Baseline ms | Optimized ms | Baseline GB/s | Optimized GB/s | Throughput gain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C library sources and headers | 8,983 / 93,836,361 | 20.436 | 17.764 | 4.59 | 5.28 | 15.0% |
| GCC 16 C++ headers | 887 / 14,780,438 | 3.695 | 3.281 | 4.00 | 4.50 | 12.6% |
| Frozen repository `src` | 7 / 732,190 | 0.213 | 0.204 | 3.44 | 3.58 | about 4% |

| Corpus | Baseline cycles/B | Optimized cycles/B | Baseline instructions/B | Optimized instructions/B |
| --- | ---: | ---: | ---: | ---: |
| C library | 1.207 | 1.049 | 3.480 | 3.204 |
| C++ headers | 1.386 | 1.230 | 3.648 | 3.351 |
| Repository | 1.608 | 1.546 | 4.179 | 4.033 |

The three C command medians were 20.436/20.461/20.403 ms for the baseline and
17.764/17.751/17.789 ms for the candidate. C++ was 3.692/3.696/3.695 ms versus
3.277/3.281/3.287 ms. The repository sample is much shorter, so its percentage is
reported approximately. Throughput gains use wall time; the C cycles/B reduction
is 13.1%. These are single-thread lexer measurements, not whole-compilation or
multicore scaling results.

The C library corpus was copied from `~/azp-bench/c-tokenizer/corpus/libc`, and the
C++ corpus from `/usr/include/c++/16`. The C library snapshot contains additional
non-C files; only the benchmark's matching files contribute to the counts above.
The repository input came from the baseline, so added tests and implementation
comments could not change the workload between executables.

## Changes

* **Pack eight character properties with GFNI.** The property table now also
  marks quotes, apostrophes and backslashes. For each 64-byte source lane, reverse
  the bytes within each qword, use `vgf2p8affineqb` with one-hot basis bytes to
  transpose eight 8x8 bit matrices, then permute bytes into eight 64-bit masks.
  Eight packed lanes are transposed into the 512-bit property bitstrings at their
  consumer. This replaces eight separate mask extraction/stores per source lane.
  Static assertions tie the property bits to the mask enumeration. Non-ASCII
  bytes still have their ASCII table results cleared.
* **Skip complete leading comments with a bounded SIMD scan.** A comment cannot
  change the first token's start-of-line flag. The scanner carries a trailing
  star between loads to recognize a split `*/`, uses masked tail loads, and
  commits only complete comments. Any backslash stops the shortcut so the
  existing lexer handles splices. Whitespace alone is not skipped, preserving the
  original boundary-placement tests. This also applies when vectorization
  resumes at a line boundary after scalar fallback.
* **Avoid analysis with no possible result.** A full interior chunk of a block
  comment can skip token analysis when it has neither a closer nor a potentially
  spliced closer. Logical line state is preserved; a newline inside a block
  comment must not end a preprocessing directive. Chunks outside literals and
  comments with no opening delimiters skip the delimiter scan and region
  arithmetic entirely.

The baseline's sampled hot instructions included the classifier's scalar mask
stores. The final disassembly contains the expected GFNI, byte permutation and
qword transpose instructions; remaining hot samples include operator-channel
classification and output offset expansion. This is consistent with moving work
away from the separate property-mask extractions. GFNI's measured Zen 5 register
form is one FP1/FP2 micro-op with 0.5-cycle reciprocal throughput according to
[uops.info](https://uops.info/html-instr/VGF2P8AFFINEQB_ZMM_ZMM_ZMM_I8.html#ZEN5).
That is supporting instruction-level evidence; the selection was made from the
9700X corpus timings.

A screening build retaining the shortcuts but removing GFNI cost 1.122 cycles/B
on the C corpus, compared with the final 1.049. Experiments that skipped empty
output lanes, packed the remaining eight masks with GFNI, or computed all output
pointers with vector prefix sums lost on the 9700X and were discarded. Shrinking
the mask array also lost throughput; the retained layout uses the original
mask indices and writes the transposed properties directly into their rows.

## Correctness

On the 9700X, the final binary passed:

* Default C and C++ lexer tests (200,000 random inputs each).
* C with `--seed 7 --fuzz 2000000` and C++ with `--seed 99 --fuzz 2000000`:
  **4.4 million random inputs** across the four runs, with zero differences.
* In every run, 70,400 snippet placements (all staying vectorized), 35,200
  guarded-page inputs, and 4,096 targeted comment cases.
* The executable smoke command, x86-64 encoder tests and benchmark, and the
  lexer benchmark on the updated repository sources.

The added comment cases sweep every source misalignment and delimiter positions
around 64-byte loads and 512-byte chunks, including a spliced closer. The three
new guarded-page templates cover leading and interior multiline comments,
preprocessor line state, and an escaped newline in a leading line comment.
Existing random streams are unchanged.

AddressSanitizer and UndefinedBehaviorSanitizer also passed 200,000 C inputs with
seed 7 and 200,000 C++ inputs with seed 99, plus the boundary, guarded-page and
comment cases, on the local Ryzen 9 7940HS. Those runs provide correctness
coverage only; all performance decisions above used the 9700X.

## Reproduction and provenance

The frozen snapshots and raw logs are in
`/home/david/buster-fast-bench/lexer-codex-20261019` on `benchpress`.
`results/final-ab.txt` contains all 18 comparison runs, `results/validation.txt`
the correctness and smoke runs, and `results/final.perf` plus
`results/final-annotate.txt` the final sampled profile. `measure.sh` releases the
lock after each invocation. Neither generated binaries nor profiles belong in Git.

From that directory, for each of `baseline` and `candidate`:

```sh
cd candidate
EXCLUSIVE_OWNER=lexer-reproduce ~/buster-fast-bench/exclusive.sh ./build.sh
cd ..
EXCLUSIVE_OWNER=lexer-reproduce ~/buster-fast-bench/exclusive.sh taskset -c 2 candidate/build/compiler benchmark-lexer corpus-libc
EXCLUSIVE_OWNER=lexer-reproduce ~/buster-fast-bench/exclusive.sh taskset -c 2 candidate/build/compiler benchmark-lexer --cplusplus corpus-cpp
EXCLUSIVE_OWNER=lexer-reproduce ~/buster-fast-bench/exclusive.sh taskset -c 2 candidate/build/compiler benchmark-lexer baseline/src
```

Recorded SHA-256 hashes (the binary hashes identify the measured builds):

* `baseline/build/compiler`: `e9ac35d97b9a1d9aefc1188f99fff0abe292a3703f3ae03a5198c14562a8bf30`
* `candidate/build/compiler`: `19b572bbdf79f861cdcda371a2de0e5e3d5b512f1c2755c7b6498c8370137d3f`
* `candidate/src/buster/compiler/c_lexer.c`: `2d21b77c5b7923e9c4214ce6955ff0d725dd715b4200aaecd4323c0775505510`
* `candidate/src/buster/compiler/c_lexer_test.c`: `b13deca56c5776b09f7db4ab4d8d6af88c6bc6fd13823806168fc073bcde4c1b`

Sorted manifests contain one `sha256  relative/path` line per regular, non-symlink
file. Their SHA-256 hashes are:

* `corpus-libc`: `fb9e4be4a7dc94427ba629b045fc38b73e5dad7e3820471f095755cd34dac5cb`
* `corpus-cpp`: `ecfc9f51e6085b306311ac032e32e4e437944715d3db8dec665135cbaaf0824a`
* `baseline/src`: `527e4fed8d209ccaae63eb4028794da6886d3bd25208a26ec9d7e437d6fad42d`
