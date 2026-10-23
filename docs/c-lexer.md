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
| C++ (`corpus-cpp`) | 8,935 / 115,057,429 | 28.616 | 24.912 | 4.02 | 4.62 | 14.9% |
| Frozen repository `src` | 7 / 732,190 | 0.213 | 0.204 | 3.44 | 3.58 | about 4% |

| Corpus | Baseline cycles/B | Optimized cycles/B | Baseline instructions/B | Optimized instructions/B |
| --- | ---: | ---: | ---: | ---: |
| C library | 1.207 | 1.049 | 3.480 | 3.204 |
| C++ | 1.379 | 1.200 | 3.741 | 3.483 |
| Repository | 1.608 | 1.546 | 4.179 | 4.033 |

The three C command medians were 20.436/20.461/20.403 ms for the baseline and
17.764/17.751/17.789 ms for the candidate. The C++ row is from the third round's
interleaved runs below, with executables built from the same two commits:
28.636/28.610/28.616 ms versus 24.912/24.897/24.945 ms. (The first measurement
used GCC 16's `/usr/include/c++/16` instead, 887 files and 14.8 MB: 3.695 against
3.281 ms, 12.6%.) The repository sample is much shorter, so its percentage is
reported approximately. Throughput gains use wall time; the C cycles/B reduction
is 13.1%. These are single-thread lexer measurements, not whole-compilation or
multicore scaling results.

The C library corpus was copied from `~/azp-bench/c-tokenizer/corpus/libc`; the
C++ corpus is `~/azp-bench/c-tokenizer/corpus-cpp`. The C library snapshot
contains additional non-C files; only the benchmark's matching files contribute
to the counts above. The repository input came from the baseline, so added tests
and implementation comments could not change the workload between executables.

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
  arithmetic entirely. (The second round removed the block-comment shortcut
  again, #63. While it was there, a skipped chunk also skipped the `/*/` check
  and C++'s raw string check, so a `/*/` across the end of a skipped chunk or an
  `R"` inside one no longer sent a part of the source to the scalar lexer: 21
  files of the C++ corpus needed it instead of 22.)

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
around the leading-comment scan's 64-byte loads and 1 to 78 bytes past 512-byte
chunk boundaries (never across one: the third round added those, #62), including
a spliced closer. The three new guarded-page templates cover leading and
interior multiline comments, preprocessor line state, and an escaped newline in
a leading line comment. Existing random streams are unchanged.

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

Recorded SHA-256 hashes (the binary hashes identify the measured builds on that
host only: `-g` records the build directory, so the same sources built in another
directory hash differently):

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
  ([#63](https://github.com/buster14a/buster/issues/63)). The next change covers such chunks without it.
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

The executable has these tools now (`./build.sh lexer-variant`, `benchmark-lexer`'s variants, `ab`, `label-spills`,
`spill-report` and `branch-misses`); [profiling.md](profiling.md) has how to use them.

# Third round: raw strings, chunks read in place and carried lanes

Measured on 2026-10-19 on the same 9700X, on top of the second round, with the same lock and pinning.

## Results

`benchmark-lexer` medians of three rounds, interleaving the executables (built from 015df42, from the first and
second rounds above, and from this round) in rotating order. All corpus checks passed.

| Corpus | Baseline | First round | Second round | Third round | Gain over baseline | Gain over second round |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C library | 20.412 ms, 1.206 cycles/B | 17.780 ms, 1.050 cycles/B | 13.798 ms, 0.814 cycles/B | 13.349 ms, 0.788 cycles/B | 52.9% | 3.4% |
| C++ | 28.616 ms, 1.379 cycles/B | 24.912 ms, 1.200 cycles/B | 19.777 ms, 0.953 cycles/B | 18.380 ms, 0.885 cycles/B | 55.7% | 7.6% |

The third round runs at 7.03 GB/s on the C corpus and 6.26 GB/s on C++, with 4.8% and 6.8% fewer instructions per
byte than the second (3.044 and 3.223). It falls back to the scalar lexer for a part of 8 C++ files instead of 17.

## Where the time went

* Sampling ops by cycles with IBS (`perf record -e ibs_op/cnt_ctl=0/`, which tags the op dispatched when the
  counter expires, so ops that wait behind a stall are sampled more) put 24% of the lexer's samples in Phase I, 23% in
  the write-out and the rest in the analysis, with no op above 1.6%. (Cycle samples without IBS piled 15% on one
  mask store of Phase I: skid.)
* Two lexer processes on the two hardware threads of one core had 1.22 times the throughput of one (a core IPC of
  4.6 against 3.7), and on two cores they did not slow each other down: one thread uses about 80% of what the core
  does for this mix of ops, and memory is not a limit.
* Inserting 32 instructions per chunk at the start of the analysis cost, once corrected for the 1.5% that any inline
  `asm` statement there costs by changing Clang's code around it, about 0.17 cycles each for a NOP, a `vpternlogd` or
  a scalar add, 0.3 for shuffles, permutes, compares into masks and mask stores, and 0.55 for a 64-byte store. The
  same 32 NOPs at the end of the iteration or spread over Phase I cost under 0.5%. So stores and FP1/FP2 ops
  are the dearest, and general-register instructions nearly free: testing the source bounds once per chunk took out
  4.5% of the instructions with no gain, as in the second round.
* In C++, 20% of the chunks went through the raw string check's scalar loop (next).

## Changes

Gains are from the A/B harness, as medians of three runs on the C and C++ corpora unless noted.

* **C++'s raw string literals checked where literals start.** The check ran before the delimiter scan on every quote
  after an identifier character, and that includes the closing quote of each string that ends with a letter or
  digit: 20% of the C++ corpus's chunks took its scalar loop, behind a branch that mispredicted on most of them, for
  0.18% that have an `R"`. A raw string literal's quote starts a literal, after an identifier character outside
  literals and comments, and the scan is right up to the first one in a chunk, so the check is now in the block for
  prefixed literals, which only literal starts after identifier characters enter. It no longer falls back to the
  scalar lexer for an `R"` in a comment or a string either. +5% on C++ (+6.4% against +1.4% for the same code without
  it); it is C++ only.
* **Chunks read in place.** Phase I copied each vector into a buffer for the analysis and the write-out, so that
  bytes outside the source read as newlines. When a chunk and the next one's first vector are inside the source (one
  test per chunk), they are now read from the source itself, and only the chunks at its edges go through the buffer:
  8 fewer 64-byte stores per chunk. About +0.4% and +0.8%. (Testing the bounds once per chunk but still copying:
  -0.3% to -0.7% on C, within 0.2% on C++.)
* **The previous chunk's bitstrings as their last lanes, in general registers.** Six bitstrings are shifted in from
  the previous chunk (and three rare ones for splices and escapes), and only their top bits matter, but they were
  carried whole, in vector registers through the iteration or spilled at its end. As 64-bit integers, 64-byte spills
  and reloads went down by 38%. +1.6% on C (+3.0% against +1.35%); on C++, -0.6% to 0.0% without the raw string change
  and +0.65% with it (+7.1% against +6.4%), which took out the scalar loop that was short of general registers. (The
  second round carried the lanes in memory: +1.1% and -1.5%.)
* **One permute to shift a vector by a byte** (`shift_in_previous`, twice per vector in Phase I): a `vpermt2b` with an
  index that Clang can't see through, where Clang emitted a `valignq` and a `vpalignr`, two FP1/FP2 uops. +0.6% and
  +0.6%.

Discarded, measured the same way (C, C++):

* 16 vectors (1024 bytes) per chunk: -1.7% to -1.9% and +1.2% to +1.5%. Per byte, 3% to 4% fewer instructions and in C 20%
  fewer mispredictions, but three times the 64-byte spills (10.5% of the ops were spills and reloads, against 7.6%),
  three times the ops from the legacy decoder (the loop outgrew the op cache), and in C far fewer chunks without
  delimiters, which skip the scan (running the scan on every 512-byte chunk costs -5.8% and -1.7%).
* One mask for the masks that are almost always empty (`%:`, the ends of 3-character punctuators, bytes above 0x7F),
  told apart from the source bytes in the chunks that have any: between -0.5% and +0.9%. Merging them takes two
  `vpternlogd` per vector, and parameter packs put `...` in 3.3% of the C++ corpus's chunks.
* Skipping the write-out of chunks without tokens: -0.1% and +0.2%; of vectors without tokens: -0.7% and -1.2%.
* One bit per vector for the write-out's rare cases and one for more than 24 tokens: -1.1% and -1.3% (unrolled,
  -0.5% and -1.1%). Phase I unrolled with the bounds tested once per chunk: 0.0% and -1.7%.
* Writing 32 positions every time, with the test at 32: -0.2% and -0.2% against 24.
* The masks loaded where first used rather than at the start of the analysis: +0.3% and -0.75%.
* The six carried lanes packed into one vector register (one `vpermt2q` per shift, five shuffles per chunk to pack
  them): +2.4% and +1.35% over the second round, between carrying them in general registers (+3.0% and +1.0%) and
  whole (+1.35% and +1.6%).
* The write-out's lanes interleaved into the analysis in two parts: -1.5% to -1.9%.
* The corpus concatenated into 256 KB files lexes 1.4% faster: about 130 cycles per file, not worth attacking.
* A cheaper index for the pair table: every two-operation form tried (c0 + 2c1, 2c0 + c1, c0 ^ c1, ...) collides for
  the 27 pairs.

## Correctness

* `test-lexer` and `test-lexer --cplusplus` on the 9700X and locally, and `--fuzz 2000000` with seeds 7 and 99 in
  both modes locally: no differences. Of the 200,000 default random C++ inputs, 120,309 need the scalar lexer for a
  part instead of 126,528 (the raw string check), with the same tokens.
* The A/B harness compared every variant's kinds, starts and ends on both corpora.
* The sweep of the review of #60: 9,244,800 checks with no difference, for each of two seeds.
* `check_comments` now also puts a closer's `*` on the last byte of a chunk, after a chunk wholly inside the
  comment too, and ends a comment with the `*/` of a `/*/` (#62): 7,040 comment cases. A lexer that dropped the
  carried `*` after a chunk wholly inside a comment failed two of them in each mode, where before only the random
  inputs caught it (12 of 200,000).

## Reproduction

The A/B harness and `trees/` are as in the second round. Also in the harness directory: `rounds.sh N name...` (N
interleaved runs and their medians), `stat_remote.sh` (dispatch-stall and misprediction counters per variant) and
`spills_cpp.sh` (the spill profile on the C++ corpus). Build and run each under `exclusive.sh`.

# Numbers without digit separators (#264)

Measured on 2026-10-22 on the same 9700X (Clang 23.1.1, `build.sh`'s build), with the same lock and pinning.

C23 and C++14 have digit separators: `1'000` is one pp-number. C17 and C++11 don't: a `'` ends a number and starts a
character constant, so clang lexes `1'000` as `1` and the unterminated `'000`. By default the lexer lexes C23's numbers
(C++14's in C++), and with `C_TOKENIZER_NO_DIGIT_SEPARATORS` C17's (C++11's). The preprocessor asks for that in every
mode before C23, its default `-std=gnu17` among them. Before the flag it lexed C23's numbers and then read every
number's bytes for a `'`, and lexed the rest of the file again from each one it found (`docs/c-preprocessor.md`,
"#109's cost").

## Changes

* **The scalar lexer**, the twin and oracle, takes no `'` into a number with the flag (`lex_numeric_constant`).
* **The chunk loop takes another property table**, as it is specialized for C++ (`tokenize_c_no_separators` and
  `tokenize_cpp_no_separators`, which `tokenize_with_flags` picks): `c_char_properties_no_separators`, which is
  `c_char_properties` without the separator-follower bit. No character follows a separator there, so
  `apostrophe_separator` and the separator at the next chunk's start are empty, and every other step is the C23 loop's:
  the two loops compile to the same instructions, in C and in C++ (checked by disassembling both). Each adds its loop
  to every translation unit that includes the lexer: 18,913 bytes for C and 22,808 for C++ in the 9700X's build.
* **`benchmark-lexer --no-digit-separators`** passes the flag to both lexers, checks their tokens against each other's
  and times the flag's loop.

## Results

`benchmark-lexer` on the frozen C library corpus (8,983 files, 93.8 MB), `main` (f8e4834) and candidate
(e9f86d6) with and without `--no-digit-separators`: two interleaved rounds, each process pinned to CPU 2 under the
9700X lock (`ab`), completed 2026-10-22. Each result is its process's median of 21 interleaved passes.

| Loop | Round 1 cycles/B | Round 2 cycles/B | Instructions/B |
| --- | ---: | ---: | ---: |
| `main`'s `tokenize_c` | 0.794 | 0.806 | 3.044 |
| `tokenize_c` | 0.786 | 0.800 | 3.046 |
| `tokenize_c_no_separators` | 0.794 | 0.802 | 3.046 |

The separator and no-separator loops run the same instructions. The no-separator copy equals `main` in round 1 and
is 0.5% faster in round 2, within the observed placement and run spread; this is no evidence of a separate speedup.
In the preprocessor, the lex phase is `main`'s again (`docs/c-preprocessor.md`, "#264's digit separators"). The frozen
candidate's lexer and benchmark source blobs (04cac7b and 38b8449) are unchanged by the takeover's parent/main merges;
those later revisions still require their own exact hosted correctness qualification.

Discarded: a loop with the separator's code taken out rather than its table. Without `apostrophe_separator`, the
`'e+` sign check and the separator at the next chunk's start, and with every unescaped `'` a literal's bound, so that
the delimiter scan no longer waited for the numbers' 512-bit addition, it ran 0.3% fewer instructions per byte but was
4% slower: 0.821 cycles per byte against the C23 loop's 0.788 on the C library corpus, the same in three rounds (IPC
3.70 against 3.87). Clang allocated its registers otherwise, with 12 more 64-byte stores to the stack, and the
preprocessor's lex phase stayed 0.1 ms above main's on `sqlite3.c`.

## Correctness

* `test-lexer` lexes every input of its checks both ways, in both languages. Its new check puts a `'` at every place of
  17 pp-numbers, before 18 followers, with line splices around it, at the source's start and around the ends of its
  first vector and chunk (80,946 cases); without separators, a case without a splice must stay on the vectorized path.
  20 fixtures pin the tokens clang 23.1.3's raw lexer gives in C17, C23, C++11 and C++26.
* `--fuzz 2000000` with seed 7 in C and seed 99 in C++, both ways: no differences.
* Mutants, each built at `-O1` and run through `test-lexer` in both languages, all fail: classify ignoring the table,
  the table keeping the follower bit on digits and on `_` (the numbers check alone catches that one in C, with the
  random inputs), the scalar lexer ignoring the flag, and C++ dispatching to the loop with separators. The discarded
  loop's mutants failed too, but for its two equivalent rewrites.
* AddressSanitizer and UndefinedBehaviorSanitizer: `test-lexer` in both languages.
* The preprocessor's checks: `docs/c-preprocessor.md` ("Lexing by the language mode").

## Reproduction

`~/buster-fast-bench/separators-264c` on `benchpress`: `bench264c.sh` builds the three trees under the lock and runs
`ab` over them (`ab-preprocessor.log`, `ab-lexer.log`); the `bins/mine-nosep` script runs this branch's
`benchmark-lexer` with `--no-digit-separators`, so that `ab` takes it as a tree. `separators-264` has the discarded
loop's runs.

## C89 and C94 mode costs (#288)

C89 disables digraphs and `//` comments; C94 retains digraphs. The three
additional chunk specializations share the separator-table choice. The ordinary
C/C++ specializations retain constant mode choices.

The frozen control was 9989bb61fb8fde70223009107d8a64dc7e53a4b6 (#283), and the
candidate eabf46ab07fa711086c62a73d36c2e7215703907. On the approved 9700X, each
build and benchmark took the shared lock separately, with timing pinned to CPU
2. The C library corpus has 8,983 files and 93,836,361 bytes. Each lexer process
reports 21 interleaved passes. Three complete alternating rounds gave:

| Default mode | Round 1 | Round 2 | Round 3 | Instructions/B |
| --- | ---: | ---: | ---: | ---: |
| Control lexer, cycles/B | 0.794 | 0.795 | 0.785 | 3.046 |
| Candidate lexer, cycles/B | 0.788 | 0.789 | 0.799 | 3.047 |
| Control SQLite preprocessing, cycles/input token | 41.69 | 41.78 | 41.80 | 132.00 |
| Candidate SQLite preprocessing, cycles/input token | 41.68 | 42.04 | 41.44 | 132.00 |

The paired differences change direction within the observed spread. These
measurements establish comparable default cost, without a speedup claim. The
original 15-round script was stopped after three complete pairs; an extra
partial fourth-round lexer receipt is excluded.

Disassembly of those target-built binaries, normalizing addresses, gives
identical default-loop instructions: 18,913 bytes / 3,289 instructions for both
C separator choices, and 22,808 bytes / 3,979 instructions for both C++ choices.
Subsequent parent/main merges preserve the candidate lexer and benchmark blobs
9cddb88e235e19c6790cdad2229d739a0ec5b916 and
fa883e1495d6b87f5da07414482aa5fdb955794c. The preprocessing numbers above remain
bound to their frozen revisions.

Two additional processes per new lexer mode gave:

| Flags after `--no-digit-separators` | Cycles/B, runs 1 / 2 | Instructions/B |
| --- | ---: | ---: |
| `--no-digraphs --no-line-comments` (C89) | 0.788 / 0.809 | 3.042 |
| `--no-line-comments` (C94) | 0.787 / 0.807 | 3.042 |
| `--no-digraphs` | 0.799 / 0.793 | 3.051 |

Every corpus comparison agrees with the scalar twin in its selected mode.
These processes take about 3.0 seconds each, excluding lock waits. A separate
SQLite `-std=c89 -march=native` process took 42.43 cycles/input token and 133.52
instructions/input token (12.046 ms vector, 24.275 ms scalar). Its modern system
headers produce C89 extra-token warnings; this is a mode-cost measurement,
not a Clang-oracle qualification.

Receipts and frozen binaries are under
`~/buster-fast-bench/codex-takeover/{control,292}` on `benchpress`:
`measurement-provenance`, `ab-default.log`, `c89-*.log`, `c94-*.log`,
`no-digraphs-*.log` and `c89-preprocessor.log`. `bench292.sh` records the original
alternating comparison; `bench292-modes.sh` records the bounded mode runs.

## GNU C raw strings (#285)

`C_TOKENIZER_RAW_STRINGS` (8) enables `R` and `LR` raw literals in C;
`C_TOKENIZER_UTF_RAW_STRINGS` (16) adds `uR`, `UR` and `u8R`, whose
prefixes need C11. C++ keeps its raw literals and user-defined suffixes;
C never takes a user-defined suffix. The ordinary C chunk specialization
still has no raw-string check.

The raw specialization checks identifier-start and quote bitstrings in the
existing rare prefixed-literal branch. It looks three bytes into the next
chunk for `u8R"`, and hands a chunk containing a raw literal to the scalar
lexer, which reads its delimiter and body. C99 conservatively detects UTF
raw prefixes too, then the scalar lexer recognizes their identifier tokens.
This bounds the extra work to chunks with possible literal prefixes and
preserves exact scalar/vector token equality.

`c_raw_string_error` is a separate diagnostic query for an `UNKNOWN` token;
the tokenizing entry points remain diagnostic-free. It reads the prefix
through translation phases 1/2, then checks the original delimiter bytes for
an invalid character, newline or excess length. A valid delimiter on an
unknown raw token means its body is unterminated. The preprocessor keeps
only these exceptional records, and reports them when their active source
context is known. Opaque headers/messages and excluded group bodies suppress them;
an untaken `#elif` directive's own line is still diagnosed.
Macro definitions diagnose once when read. Guard-page tests cover every
truncation of the diagnostic fixtures, all raw-prefix modes and physical
offsets after a spliced prefix. These changes leave the vector chunk loop
and tokenizer output unchanged.

`test-lexer --raw-strings` and `--raw-strings-c99` cover raw literals,
near misses, randomized pieces and every placement around 64-byte and
512-byte boundaries, with guarded pages. `benchmark-lexer` accepts the
same flags so the specialization can be timed directly. The preprocessor
covers modes, option order, pasting, stringizing, skipped directives,
splices/trigraphs, dollar punctuation, `_Pragma`, line names and printers.
An invalid delimiter containing a line splice pins its verbatim spelling.

### Frozen target-host measurements

The candidate is 281e245d9a1e19412689d85666dede7d79875fe8, built on the approved
9700X with the shared lock. Three processes per configuration, pinned to CPU 2,
compare its lexer with a same-process `control` variant from the C89 lexer at
1051292. Both see the same 8,983 files / 93,836,361 bytes and 13,152,141 tokens.
Each process takes 21 rotating passes. All scalar/vector/variant corpus
comparisons agree.

| Configuration | Candidate cycles/B, runs 1 / 2 / 3 | Control cycles/B, runs 1 / 2 / 3 | Candidate / control instructions/B |
| --- | ---: | ---: | ---: |
| Ordinary C, no RAW flag | 0.789 / 0.797 / 0.793 | 0.789 / 0.790 / 0.788 | 3.048 / 3.045 |
| `--raw-strings` | 0.797 / 0.801 / 0.795 | 0.789 / 0.787 / 0.785 | 3.076 / 3.045 |
| `--raw-strings-c99` | 0.793 / 0.798 / 0.797 | 0.787 / 0.787 / 0.792 | 3.076 / 3.045 |

The ordinary copy differs from its control by up to 1% between processes,
showing the remaining code-placement spread. RAW-enabled modes retire about
0.9% more instructions than the candidate's ordinary mode. The raw feature
therefore has a small explicit cost; these results establish neither a speedup
nor a no-cost GNU raw path. Each process takes about 3.45–3.52 seconds,
excluding lock waits.

A separate alternating A/B uses the **same candidate binary** for SQLite
preprocessing, with default GNU raw strings enabled or
`-fno-raw-string-literals`. All parent/include/recovery implementation is shared
between the two controls. `-E -P` output hashes match byte for byte before the
timings (SHA256 8cc652b634970b9bd71497309e5d33ffe54459edbc3356c260a6e61dbd550bee).

| SQLite vector preprocessing | Round 1 | Round 2 | Round 3 | Instructions/input token |
| --- | ---: | ---: | ---: | ---: |
| Raw disabled, cycles/input token | 41.73 | 41.73 | 41.93 | 133.49 |
| Raw enabled, cycles/input token | 42.02 | 41.86 | 42.24 | 133.94 |

The paired time cost is about 0.3–0.7%, and the instruction cost 0.34%. These
frozen receipts measure the raw feature without attributing unrelated parent
changes to it. The candidate binary SHA256 is
0f106e7f750b0e6425f1d0e9ac1cc4f6fe7b4d95be4f3711a10dd629d467f38f;
the SQLite input SHA256 is
b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189.

The frozen candidate passes 319 built-in cases against Clang, the fresh/cold
shared/warm shared private-view checks, both kernels' 8/9 query and 64/65 direct
repair boundaries, all 70 option sets and the three saved minimized angle-header
fuzz failures. A one-case fuzz batch per language also checks the harness; the
standard random campaign belongs to the final exact-head hosted workflow.
Later header-CLI/oracle integration gets that separate correctness validation.
The measured lexer and benchmark source blobs remain
982029ded5c4b2a0f8a0677c6313778b6f21268f and
38a5a9572938a14565a40475647e3b28e713803f.

Receipts are under `~/buster-fast-bench/codex-takeover/293` on `benchpress`:
`raw-measurement-provenance`, `raw-fixed-{default,gnu11,gnu99}-*.log`,
`raw-preprocessor-provenance`, `raw-preprocessor-ab.log`,
`raw-fixed-preprocessor.log` and `saved-repro-*.log`.
`bench293-lexer.sh` and `bench293-preprocessor.sh` reproduce the bounded runs.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). The
[raw API](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_lexer.c#L425)
and [actual file producer](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_preprocessor.c#L2381)
make these implemented edges:

| Producer → consumer | Representation and completion |
| --- | --- |
| File read/map or `pp_run_source` → `c_tokenize` | Immutable counted bytes, no readable padding or NUL required. The backing file/cache or supplied bytes must survive all token/spelling readers. |
| `c_tokenize_vectorized` plus scalar recovery → `pp_lex_for_mode` → `refine_tokens` | Three disjoint columns: byte kind/start-of-line flag, u32 start, u32 end, source order; successful nonnegative count publishes the logical extent. Mode recovery can split/re-lex before refinement. |
| Lexer columns → scalar/reference tests and benchmarks | The same raw contract; a negative result, including the raw vector-only decline, is not a publishable stream. |

Each output column needs `length + 128` elements, not merely the number of
logical tokens. This authorizes the lexer's whole-vector **stores**, not
reads past source bytes. Source and all output columns are nonoverlapping;
positions require `length <= UINT32_MAX - 128`. `pp_prepare_into` supplies
64-byte-aligned columns, then trims their reservation to actual count plus
slack before allocating leading-space words. Source alignment is unrestricted;
the source tail is bounded by the lexer. Scalar recovery retains token order
and exact physical source spans; it is not a second whole-file lexer pass in
the common case.

The logical batch is a file. Chunk carry includes lexical context, so an
arbitrary split of source bytes is not a legal independent lexing partition.
The implementation's fixed-width chunks, final partial chunk and recovery
windows are internal. Full stores into provisioned output slack do not turn
those entries into tokens. Empty source still needs the API's output capacity
where the caller provides buffers; do not infer a null-pointer contract from
a zero logical count.

The producer retains starts/ends and line-start state, but refinement still
classifies identifier kinds, compacts spans and scans for backslashes. Those
are observed passes, not proof that exporting more lexer masks would help:
normalization depends on mode, and most consumers do not need dense identifier
metadata. Compare retaining a mask or fusing bounded preparation with the
current in-place handoff under [#116](https://github.com/buster14a/buster/issues/116), including memory traffic and lifetime.
Do not extend the raw API merely to improve `benchmark-lexer`.

Disposition: retain the raw columns and bounded source contract. Existing
lexer guarded/chunk/alignment and Clang checks plus refinement's independent
oracle cover this handoff; whole-file preparation/refinement and actual
preprocessing costs belong to [#116](https://github.com/buster14a/buster/issues/116)/[#117](https://github.com/buster14a/buster/issues/117). Contextual raw-string recovery remains
with [#285](https://github.com/buster14a/buster/issues/285). Kernel ns/byte does not establish joint lex/refine/PP throughput.
