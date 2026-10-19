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

# Second round: branches, write-out, punctuators, spills and the delimiter scan

Measured on 2026-10-19 on the same 9700X (Clang 23.1.1, `build.sh`'s build), on top of the changes above, with the
same lock and pinning. This round worked on Phases II and III; in Phase I it only changed what the write-out reads.

## Results

`benchmark-lexer` medians of three rounds, interleaving the executables (built from 015df42, from the first round
above, from this round's state before its last three changes, and from this round) in rotating order. All corpus
checks passed: no file differs from the scalar lexer.

| Corpus | Files / bytes | Baseline | First round | Second round | Gain over baseline | Gain over first round |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C library (`~/azp-bench/c-tokenizer/corpus/libc`) | 8,983 / 93,836,361 | 20.415 ms, 1.206 cycles/B | 17.844 ms, 1.054 cycles/B | 13.763 ms, 0.812 cycles/B | 48.3% | 29.7% |
| C++ (`~/azp-bench/c-tokenizer/corpus-cpp`) | 8,935 / 115,057,429 | 28.727 ms, 1.384 cycles/B | 24.974 ms, 1.203 cycles/B | 20.046 ms, 0.965 cycles/B | 43.3% | 24.6% |

The second round runs at 6.82 GB/s on the C corpus and 5.74 GB/s on C++, at about the same instructions per byte
as the first (3.198 against 3.204 for C): the gains are from fewer mispredictions and spills (IPC 3.04 to 3.94). It
also falls back to the scalar lexer for a part of fewer files: 1 instead of 5 in C and 17 instead of 21 in C++.

## Where the time went

Profiles of the baseline lexer on the 9700X: 37% of the dispatch slots retired, 44% were stalled by the back end
and 10% lost to mispredictions, about 3 per 512-byte chunk. Last-branch records (`perf record -j any,u`) named the
branches: the write-out's test for more than 16 tokens in a vector and its test for a multi-character punctuator,
and conditions written with `||` or `&&` (or `? :` on a flag) whose operands depend on the source, which Clang
compiles to a branch per operand. Branching on such data costs more than computing all of it.

Then the stalls were mostly the FP scheduler (full on 20% of cycles) and the store queue (13%). Sampling ops with
IBS (`perf record -e ibs_op//`, which attributes each sample to its exact instruction) on a build whose spills and
reloads were labelled showed that 12.7% of the lexer's ops were register spills and reloads, including about 80
64-byte spill stores per chunk: a third of the chunk's stores.

uops.info's Zen 5 data explains the write-out's costs: compares into masks, permutes, shuffles, compresses and expands
all issue to the same two pipes (FP1 and FP2); loading a mask register from memory takes one FP1/FP2 uop but from a
general register two; and `vpbroadcastd` from a general register is two uops with an 8-cycle latency.

## Changes

Each gain below is from the A/B harness described under reproduction, as cycles/B on the C and C++ corpora,
against the step before it. The first two were measured on the baseline, then together on top of the first round;
the others on top of the first round and the steps before them.

* **Write-out (Phase III).** Masks go from memory straight to mask registers; the vector offsets advance by an add
  instead of a broadcast per vector; the token counts of all eight vectors come from one `vpopcntq`; the first 24
  positions are written every time, so that the branch is at 24 tokens (0.6% of vectors) rather than 16 (9%).
  +2.4% and +3.4% on the baseline.
* **Branch-free chunk state.** The next chunk's state, the start-of-line carry and the checks for prefixed literals
  and raw strings are computed with bitwise operators (`bit_0_if` builds a bitstring from a flag), so each
  condition is one well-predicted branch or none. +5.4% and +5.9% on the baseline after the write-out change, and
  the two together +7.6% and +5.1% on top of the first round. (The prefixed-literal check alone mispredicted on more
  than half of the taken executions in the last-branch records.)
* **Exact tests.** `(starts | ends) > 24` also took the slow path when neither count exceeded 24 (17 | 8 is 25), and
  the first round's test for delimiters was a short-circuit `||`. +2.1% and +1.0%.
* **Write-out a chunk later.** Phase III of a chunk runs after the next chunk's classification and before its
  analysis (`ChunkTokens`, `write_chunk`): the analysis is mostly long dependency chains, and the write-out a lot of
  independent work, which can now overlap them. The chunks' vectors get a third buffer. +1.4% and +1.8%.
* **One branch for a vector's rare kinds.** Dot-numbers and prefixed and unknown literals share a per-vector bit
  (`special_lanes`). +1.4% and +0.4% (measured with the multi-character punctuators in it too).
* **Chunks without backslashes.** 97% of the C corpus's chunks have none, and then have no line splices or escapes:
  one test skips their analysis, including a 512-bit subtraction on the way to the delimiter scan. +1.1% and +0.1%.
* **Multi-character punctuators by table lookup.** They are in 14% of the C corpus's vectors, and the test for them
  was the largest remaining misprediction. Every vector now looks up its 2-character punctuators in a 128-entry table
  (`c_pair_kinds`) indexed by (5 · c0 + 4 · c1) mod 128 of each character and the next: four byte adds and one
  `vpermi2b`. The next characters come from an unaligned load one byte into the chunk's vectors, which are
  consecutive in memory, so no shuffle is needed. A search over small multipliers found that index distinct for all
  26 pairs that the channel tables make punctuators, and for C++'s `.*`. The 3- and 4-character ones (0.011 per chunk
  in C, 0.24 in C++) are told apart by their first two characters, so a 64-entry table (`c_long_kinds`, one
  `vpermb`) at the same index serves them behind a branch. This replaces the multiplicative hash of up to 16
  compressed starts and its expansion. +2.8% and +5.6%; mispredictions went from 1.87 to 0.87 per chunk.
* **Fewer spills.** After the chunk state is known, a compiler barrier (`__asm__ volatile("" ::: "memory")`) makes
  Clang load the masks again from the mask buffer instead of keeping them in registers since the start of the
  analysis and spilling them to other stack slots, and the punctuator masks are only loaded there. `splice_joins` and
  `highest_bit_below` are inlined: as calls on rare paths, they clobbered every vector register, which made Clang
  store the live bitstrings on the common path. Spill and reload ops went from 12.7% to 7.8% of the lexer's ops, and
  64-byte spill stores from about 80 to 30 per chunk. +2.7% and +3.4% (without the inlining, the barrier lost 2.0%
  and 1.0%).
* **No shortcut for chunks inside a block comment.** With the changes above, the first round's shortcut cost 0.8%
  to 1.2% on C and 0.3% to 0.7% on C++ (two runs of 31 passes), and it reset 13 pieces of chunk state by hand
  ([#63](https://github.com/buster14a/buster-fast/issues/63)). The next change covers such chunks without it.
* **A delimiter scan that takes one round and starts after the comment the chunk is inside of.** Of the chunks that
  have delimiters, the scan took two rounds in 89% of its calls in C, the second finding nothing; it now tests
  whether a cursor finds another delimiter before going round again, and one round does in 98% of them. The block comment that a
  chunk starts inside of (22% of those chunks in C) is found first, by one subtraction from its first `*/`, and the
  scan starts after it; before, the scan treated each of the comment's lines as starting outside it, and in 68% of
  those chunks in C (97% in C++) found that a comment spanned lines and scanned the chunk again. A chunk inside a
  comment then has no delimiters left, so the test for chunks without delimiters (now one vector test) skips it.
  +3.0% and +2.5%.
* **Splices after whitespace.** A line splice after a space or tab, as in most multi-line macros, can't join tokens
  or close a comment, so only the others are checked one at a time. +1.1% and +0.6%.

Mispredictions went from 2.96 per chunk in the baseline to 0.82. The largest remaining one is the test for chunks
without delimiters (a scan that never skips costs more than its mispredictions). The FP scheduler is now full on 24%
of cycles and the store queue on 7.6%, with about 204 stores per chunk; spill and reload ops are 7.4% of the total,
most of them general registers, which matter less: testing the source bounds once per chunk instead of for each
vector took out 4.4% of the instructions with no change in time.

Discarded, measured the same way (C, C++, against the variant each was tried on):

* Writing 32 positions every time instead of 24, with the test at 32: +0.4% and -0.4%, against +1.6% and +0.8% for
  the exact test at 24.
* A branch at 16 tokens instead of writing 24 every time: -0.4% and -0.8%.
* Phase III after the next chunk's analysis rather than before it: +0.0% and +2.9%.
* Reloading the vectors from the source in Phase III instead of keeping them (fewer stores): -2.9% and -0.5%, as
  the loads' boundary tests cost more than the stores.
* Unrolling the write-out's vector loop: +0.3% and -0.4%. Forcing a single permute to widen positions 16 to 23
  (Clang emits an extract and a zero-extension): -0.1% and -0.2%.
* The 3- and 4-character table on every vector instead of behind a branch: 0.2% and 0.7% below the branch.
* The write-out as an out-of-line call: -6.0% and -3.5%.
* Carrying only the top lane of the bitstrings that pass between chunks, in memory: +1.1% and -1.5% (all six), or
  -0.5% and -1.6% (the three masks only).
* Testing once per chunk whether its vectors are inside the source, instead of in `load_lane` for each one: -0.1%
  and -0.7%, with 4.4% fewer instructions.
* Again with the later changes, a branch at 16 tokens instead of writing 24 every time: -2.2% and -0.8%.

## Correctness

* `test-lexer` and `test-lexer --cplusplus` on the 9700X and locally, and `--fuzz 2000000` with seeds 7 and 99 in
  both modes locally: 8 million random inputs with no differences, each run also covering the 70,400 snippet
  placements, 35,200 guarded-page inputs and 4,096 comment cases. The boundary snippets contain every punctuator
  that the tables handle.
* The A/B harness compared the kinds, starts and ends of every variant with the original lexer's on both corpora.
* The sweep of the review of #60 (`~/buster-fast-bench/review-pr60/harness/review_harness.c`): closers, `/*/`, `/**/`,
  `*/*`, spliced closers, `R"(` and `1'2` across chunk ends at every alignment, leading comments and long comments, in
  C, C++ and trigraph mode, aligned and between guard pages: 6,004,800 checks with no difference.
* The smoke command, `test-x86-encoder` and `benchmark-lexer src`.

## Reproduction

On `benchpress`, `~/buster-fast-bench/lexer-claude` has the A/B harness: `build.sh name...` compiles each
`variants/<name>/c_lexer.c` as its own translation unit into `out/lexbench`, which checks every variant against the
first on a corpus and then times them in 21 interleaved passes in one process (`--cplusplus`, `--passes N`,
`--only name`). `spills.sh name`, run from a copy of the directory on another machine (it drives `benchpress` over
ssh), labels a variant's spills and reloads in its assembly, samples it with IBS and lists the hot spill slots. `trees/` has the trees of the results table, and `trees/run_round.sh` the interleaved rounds.
Build and run each under `exclusive.sh`.
