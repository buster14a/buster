# Profiling on the 9700X

How the lexer's and the encoder's optimization rounds were measured and profiled, and the tools this executable has
for it. [c-lexer.md](c-lexer.md) and [x86-64-encoder.md](x86-64-encoder.md) have what the rounds found; this is the
method, so that the next stage can be tuned the same way. The commands are `build.sh lexer-variant`,
`benchmark-lexer`'s variants, `ab`, `hot-loops`, `label-spills`, `spill-report` and `branch-misses`
(`src/buster/compiler/profile.c`); `test-profile` checks them.

## The rules

- **Decide on the 9700X.** Zen 5 runs 512-bit operations on four full-width pipes, so Zen 4's numbers mislead (merge-masked
  loads and `vpcompressb` to memory, for two, cost very differently), and models of the CPU from instruction tables,
  llvm-mca among them, are well off (x86-64-encoder.md, "Zen 5's costs, measured"). A laptop is for building,
  testing and reading code; every performance decision comes from runs on the 9700X.
- **One run at a time.** Every build, test, benchmark and `perf` run there goes through
  `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`, one lock per run (AGENTS.md, "Sharing the
  9700X"), pinned to CPU 2 (`taskset -c 2`), the CPU with the `performance` governor and its SMT sibling offline.
- **Measure in time as well as cycles, in both states.** After 30–300 ms of lighter code the clock boosts, and heavy
  AVX-512 loops then run 5–50% slower for 5–20 ms, which #199's measurements are consistent with a per-core power cap
  causing. The encoder's and dataflow's benchmarks take `--after-scalar MS` for that state and `--only NAME` for the
  back-to-back one; [the coding style](coding-style.md) ("SIMD and Zen 5") says how to measure both, and
  x86-64-encoder.md ("Both states") has the encoder's.
- **Measure again after merging main.** In #35, a change of 16 ANDs per group that had measured free cost 2.4% once main's
  register allocation changed under it.

## Comparing two versions

The noise sets which comparison to use.

- **Variants in one process: up to about 1.5%, with a control.** Variants that take turns in one process see the same
  clock, caches and corpus, but not the same placement: a copy of the tree's own lexer built as a variant measured
  1.3–1.5% faster than `c_tokenize` on the 9700X (the C library corpus, 2026-10-22), where two copies in the old harness
  differed by about 0.6%. So build a copy of the reference as one of the variants and read the others against it.
  `./build.sh lexer-variant NAME=SOURCE...` builds `build/compiler` with each SOURCE, a copy
  of `c_lexer.c`, as a translation unit of its own (`c_lexer_variant.c`, exporting `c_lexer_variant_NAME`), and
  `benchmark-lexer` then checks each against the tree's `c_tokenize` on every file and times them all in 21 passes,
  in an order that rotates by pass. Each variant's line ends with its change from `c_tokenize`, of the median cycles and
  as the median of the passes' ratios (positive is faster). A variant that tokenizes any file differently fails the
  command. Keep the copies outside `build/`, which `build.sh` deletes:

  ```sh
  cp src/buster/compiler/c_lexer.c ../variants/k1.c   # then edit k1.c
  EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh ./build.sh lexer-variant control=src/buster/compiler/c_lexer.c k1=../variants/k1.c
  EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-lexer ~/azp-bench/c-tokenizer/corpus/libc
  EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-lexer --cplusplus ~/azp-bench/c-tokenizer/corpus-cpp
  ```

  A variant from another tree must keep `c_tokenize`'s interface. Treat a difference within the control's gap as noise.
- **Builds in turn: about 2%.** The same encoder build measured +2.3% to +5.3% against main over five runs of 7 rounds,
  and a single round can be 5–10% off. `build/compiler ab` runs several trees' builds in turn:

  ```sh
  ./build/compiler ab --rounds 15 --cpu 2 --lock ~/buster-fast-bench/exclusive.sh ../main ../mine \
      -- benchmark-x86-encoder --only x86_encode_64 -- benchmark-x86-encoder --vector --only x86_encode_64
  ```

  Each TREE is a checkout with its own `build/compiler` (or an executable), and each `--` starts an argument list. Every
  round runs every list on every tree, the trees in an order that rotates by round, each run pinned to `--cpu` and
  through `--lock`, which takes the lock for that run alone, so a CI job can take it between two runs; don't run `ab`
  itself under the lock. The output is each run's, after a heading (`== round 3 tree ../mine config [...]`); it exits
  with status 1 if a run fails. For a small difference, time one loop (`--only`, about 0.1 s a run) over 15 rounds,
  and look at an outlier with `perf stat` before believing it.
- **Whole drivers: `time-runs` under `ab`.** A change to a phase that a few percent of a compile takes has to show in
  the whole driver, a fresh process each time, startup, page faults and teardown included (#335). `time-runs` runs
  this executable once per argument list in each run, the lists in an order that rotates by run, after one warm-up
  each, and reports every process's wall time (fork to `wait4`), user and system time, peak resident set and page
  faults, then each list's medians:

  ```sh
  ./build/compiler ab --rounds 5 --cpu 2 --lock ~/buster-fast-bench/exclusive.sh ../mine \
      -- time-runs --runs 21 --list parse -fsyntax-only file.c --list parse -fsyntax-only --scalar file.c
  ./build/compiler ab --rounds 5 --cpu 2 --lock ~/buster-fast-bench/exclusive.sh ../main ../mine \
      -- time-runs --runs 21 --after-scalar 200 --list -fsyntax-only file.c
  ```

  Its lists start at `--list`, since `ab` takes `--` for its own. Two lists in one binary compare an option; two
  trees under `ab` compare builds. `--after-scalar MS` runs an untimed scalar loop for MS ms before each timed process,
  on the same CPU, the state a compiler starts in after lighter work; `--executable PATH` times another program. A
  process whose exit status isn't its list's first warm-up's fails the command. Compare the five rounds' medians:
  their spread is the noise.

## Counting in the process

The benchmarks read hardware counters around what they time with `perf_event_open`, and `rdpmc` from the event page
when it allows it (a read with `clock_gettime` and five counters costs about 233 cycles; subtract it). The NMI
watchdog holds one of the six core counters, so five events count without multiplexing; check `time_enabled` against
`time_running`. Never run `perf stat` with its own events beside them: both multiplex, and every number is wrong.
AMD's raw configurations put the event's low byte in bits 0–7, the unit mask in 8–15 and the event's high nibble in
32–35; on Zen 5:

| Event | Config |
| --- | --- |
| `ex_ret_ops` | `0xc1` |
| `de_no_dispatch_per_slot.no_ops_from_frontend` | `0x1000001a0` |
| `de_no_dispatch_per_slot.backend_stalls` | `0x100001ea0` |
| `ex_ret_brn`, `ex_ret_brn_misp`, `ex_ret_brn_tkn` | `0xc2`, `0xc3`, `0xc4` |
| PMCx1A0 umask 0x08, read as the dispatch slots the power cap takes (inferred, #199) | `0x1000008a0` |

Zen 5 has 8 dispatch slots per cycle (Zen 4 has 6). `perf stat -e de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall`
counts cycles the FP scheduler was full, and `..._part2.retq` cycles the reorder buffer was: these told the encoder's
fixup interface's latency problems apart. Once its mispredictions were gone, the lexer's stalls were the FP scheduler
(full on 20% of cycles) and the store queue (13%).

## Where the time goes

### The hot loop: `hot-loops`

```sh
perf record -o hot.data -e cycles:u -c 100003 -- taskset -c 2 ./build/compiler benchmark-x86-encoder --only x86_encode_64
perf script -i hot.data -F ip,sym,symoff,dsoff > hot.txt
objdump -d -p --no-show-raw-insn build/compiler > compiler.dis
./build/compiler hot-loops x86_encode_64 compiler.dis hot.txt
```

`hot-loops` ranks the function's loops (each backward jump's range) by their samples and prints the smallest loop
with at least half of them, instruction by instruction with its samples: enclosing loops tie with the hot one, and the
one to read is the innermost. A static function has a copy in every translation unit that includes its file (the
lexer's `tokenize_c` has four), and `objdump --disassemble=NAME` (binutils 2.47) prints only one, so give it the whole
file's disassembly, with its program headers (`-p`): each sample's file and offset in it (`dsoff`), through the file's
LOAD segments (offsets and addresses differ in lld's executables), give its address, which says which copy it is in,
and it takes the busiest. Only samples of the disassembled file count, by its path: objdump's first line, resolved from
the directory the command runs in, against perf's absolute path. Shared libraries' samples, and another tree's
`build/compiler` in an `ab` run, don't.
`--loops N` sets how many loops it ranks. The encoder's rounds read the loop this way to count its instructions and stack
references, and found that clang had stopped unrolling it.

### Stalls: IBS

Samples of `cycles` skid: in the lexer's third round, 15% of them landed on one mask store. AMD's instruction-based
sampling tags the operation itself:

- `perf record -e ibs_op/cnt_ctl=0/` samples by cycles, tagging the operation dispatched when the counter expires, so
  operations that wait behind a stall are sampled more. It shows where the stalls are, which isn't always what causes
  them: in the parse spike's K4, samples piled up where the reorder buffer filled, not on the latency at its root
  (a variant that kept K4's timing without its stack disproved the theory the samples suggested).
- `perf record -e ibs_op/cnt_ctl=1/` samples dispatched operations uniformly, so each operation's share of the samples
  is its share of the operations: what `spill-report` needs.

The 9700X allows IBS (`perf_event_paranoid` is -1 there); on a Zen 4 laptop at 2, perf couldn't open it.

### Spills: `label-spills` and `spill-report`

Clang marks every spill and reload in its assembly (`# 64-byte Spill`, `# 8-byte Folded Reload`). `label-spills` puts a
label before each one of a function's, and the program built from that assembly then has a symbol at every spill, so
that the disassembly shows where each spill is. The labels carry a tag, the variant's name (`buster_spill.k1.0`), and
the assembly is otherwise unchanged. `spill-report` finds the labeled copy by address (the function's symbol before its
first label, up to the next symbol that isn't one of its labels) and counts samples by their offsets in the file, so
neither the function's copies in other translation units nor another labeled variant count. It needs the symbol table
too (`-t`): a spill that is the function's first instruction has its label at the function's address, where objdump's
headers name only the function. `build.sh lexer-variant`
refuses two labeled variants of one tag:

```sh
./build.sh lexer-variant-assembly k1 ../variants/k1.c k1.s
./build/compiler label-spills tokenize_c k1 k1.s k1_labeled.s
./build.sh lexer-variant k1=k1_labeled.s
perf record -e ibs_op/cnt_ctl=1/ -c 100003 -o spills.data -- taskset -c 2 ./build/compiler benchmark-lexer ~/azp-bench/c-tokenizer/corpus/libc
perf script -i spills.data -F ip,sym,symoff,dsoff > spills.txt
objdump -d -p -t --no-show-raw-insn build/compiler > compiler.dis
./build/compiler spill-report tokenize_c k1_labeled.s compiler.dis spills.txt
```

(`lexer-variant-assembly` writes the variant's assembly with the build's flags and debug information; the build's own
lexer is a variant of `src/buster/compiler/c_lexer.c`.) `spill-report` gives the share of the labeled copy's samples
that are at its spills and reloads, and each stack slot's, as zmm, ymm, xmm, k or general registers, with the source
location of its hottest spill and reload (`--slots N` of them). In the lexer's second round 12.7% of its operations were
spills and reloads, about 80 64-byte spill stores per 512-byte chunk; two changes brought that to 7.8%: an empty `asm`
with a memory clobber, after which the masks are read again from their buffer (clang otherwise copied them into new
spill slots), and forcing the rare paths' helpers inline (a call clobbers every vector register, so clang spilled the
live bitstrings on the hot path around it). The static counts (`-Rpass-missed=regalloc`, the assembly's comments)
didn't predict the gains; the runs did. The commands above, on the 9700X on 2026-10-22, found 6.8% of the current lexer's sampled
operations at its 560 spills and reloads on the C library corpus.

### Mispredictions: `branch-misses`

Zen 5 records the last branches (`perf record -j any,u`), with whether each was mispredicted:

```sh
perf record -j any,u -e cycles:u -c 100003 -o lbr.data -- taskset -c 2 ./build/compiler benchmark-lexer ~/azp-bench/c-tokenizer/corpus/libc
perf script -i lbr.data -F brstack > brstack.txt
perf script -i lbr.data --show-mmap-events | grep -m1 'r-xp.*build/compiler'
objdump -d --no-show-raw-insn build/compiler > compiler.dis
./build/compiler branch-misses brstack.txt --bias BIAS --disassembly compiler.dis
addr2line -i -e build/compiler ADDRESS
```

The mapping's line reads `[START(LENGTH) @ OFFSET ...]: r-xp .../build/compiler`, and the load bias is START less
OFFSET (hexadecimal, without `0x` or with it): `branch-misses` takes it from each branch's address, so that it ranks the
branches at the file's addresses, with their instructions from the disassembly (`--branches N` of them). The records
are of taken branches, so a branch mispredicted when not taken doesn't show. The lexer's baseline mispredicted about 3
times per 512-byte chunk, 10% of its dispatch slots: the write-out's tests and conditions written with `||`, `&&` or
`?:` on the source's bytes, which clang compiles to a branch per operand. Computing all of it without branches was the
largest gain of its second round.

### Paths that should be rare

Count how often each `BUSTER_UNLIKELY` block runs on the corpora before trusting the word: in the lexer's third round,
the C++ raw string check's scalar loop ran in 20% of the chunks, because its test before the scan also matched closing
quotes. Count it with a counter in a variant (`build.sh lexer-variant`), whose tokens the corpus check still compares.

## Reading code without the 9700X

clang 23 with `-march=znver5` on another machine gives the 9700X build's code instruction for instruction (checked for
`x86_encode_64` in #35), so variants can be compared by their code before they are timed: compile the unit with
`build.sh`'s flags, find the hot loop and count its instructions and stack references, and send only the finalists to
the 9700X. The count isn't the time: in #35 variants with the same operations differed by 10 instructions per group and
2% in cycles, from register allocation, and clang's code for the encoder's loop changes a lot with small source
changes, so compare every width after changing it.

To see whether a change moved a timed function, compare the builds by address range (`objdump -d --start-address
--stop-address` over the unit's span, addresses and symbol offsets removed), not by name: every static copy of a name
has its own code, and a change to a later unit's strings moves `lea`s in unrelated ones.

## Traps

- **Probes change the code.** An `asm volatile` statement, even `.nops 64`, made clang compile the lexer's loop around
  it differently, about 1.5%; subtract that before reading a marginal cost from a probe. Corrected, 32 instructions per
  chunk cost about 0.17 cycles each for a NOP, a `vpternlogd` or a scalar add, 0.3 for shuffles, permutes, compares
  into masks and mask stores, and 0.55 for a 64-byte store.
- **The predictor learns.** Timing a branchy stage again and again on a small input lets Zen 5's predictor memorize it:
  libc files of about 5,000 tokens showed 17 times fewer mispredictions per token than sqlite3, and 33,000 records
  sized 13 times a sample made a scalar loop 3 times too fast. Time each input once per sample, over inputs larger
  than it learns, and report a corpus of code beside one of declarations.
- **Placement.** Branchy scalar loops run slower at some 64-byte offsets, in states that differ between processes, and
  stack slots or rows that share their low 12 bits with stores alias. A difference that comes and goes between builds of
  the same source is probably one of these (dataflow.md, "Where the scalar twin's code falls").
- **The stack's page offset.** Address space randomization starts each process's stack at another offset in its page,
  and a stage can then run fast in some processes of one build and slow in others. With the stack placed at every 64th
  byte of its page, `benchmark-checker-source`'s body checking on `sqlite3.c` ran 3 to 13% slower at seven offsets from
  208 to 656 bytes (all those sampled but 528) and 4% at 2,832, with the same instructions and more store-to-load
  conflicts (`ls_bad_status2.stli_other`, loads that couldn't take an older store's data). That is consistent with stack
  slots that share their low 12 bits with the data the loop stores (4 KiB aliasing), though the record doesn't identify
  the loads and stores. With randomization, 8 of 32 processes of a campaign were over 2% slower, in main and #424's head
  alike, while the whole driver's `-fsyntax-only` moved by at most 1.5% over its own 64-offset sweep (#445, [the
  record](https://github.com/buster14a/buster-performance/tree/e0f03a362c29568db3230b265600b2085afe6ce0/docs/measurements/checker-source-slow-state-445-2026-10-26)).
  Compare such a stage's medians over many processes, or place the stack alike in every process: `setarch -R` with the
  same argument and environment bytes (both executables at paths of the same length, and an environment of a chosen
  length, as the record's `procbench` gives them with `norandom` and `pad=N`), each process's offset checked
  (`/proc/PID/stat`'s startstack modulo 4,096, which `procbench` records), and the builds compared at several offsets.
- **Two threads.** Two lexer processes on one core's two hardware threads had 1.22 times the throughput of one: one
  thread uses about 80% of what the core does for that mix, so SMT is no lever.
- **A phase's cost is in context.** The difference between runs that stop after a phase and before it is the phase's
  cost where it runs; the same code elsewhere can cost more (branch sizing, #144). Measure the swap itself before acting
  on a table of phases.

## Explaining with uops.info

Explain a measurement with uops.info's measured Zen 5 data: the instruction's page
(`https://uops.info/html-instr/VPERMD_ZMM_ZMM_ZMM.html`) or its `instructions.xml`, read with a streaming parser. The
lexer's write-out was explained this way: compares into masks, permutes, shuffles, compresses and expands all issue to
FP1 and FP2; `kmovq k, m64` is one FP1/FP2 operation but `kmovq k, r64` two; `vpbroadcastd zmm, r32` is two operations
with an 8-cycle latency. x86-64-encoder.md has the pipe and latency tables measured on the 9700X itself. Two cautions:

- An `<architecture name="ZEN5">` element isn't a measurement: 191 variants have only `<doc>` rows, from AMD's tables,
  and some are wrong (RTM's `xbegin` family and the `rdmsr` with an immediate, which Zen 5 doesn't have). Read
  `<measurement>`, which can disagree with the doc row (`vpmullq zmm`: 2 cycles measured, 3 documented). Some
  instructions Zen 5 runs have no ZEN5 entry (far `call`, `jmp` and `ret`, `wbnoinvd`, `fninit`).
- Intel's XED stops at Zen 2 in its AMD chip models, so it can't say what Zen 5 runs: AMD's manual can.

## Where these came from

The tools replace scripts the rounds ran on the 9700X from `~/buster-fast-bench/` (written by agent sessions in October
2026, never in a repository): `lexer-claude`'s `build.sh`, `variant.c` and `harness.c` (`./build.sh lexer-variant`
and `benchmark-lexer`'s variants), `remote.sh`, `rounds.sh`, `trees/run_round.sh` and `x86-opt/ab.sh` (`ab`),
`x86-opt/hot.sh` and `hot2.sh` (`hot-loops`), `spills.sh` (`label-spills` and `spill-report`) and `lbr_remote.sh`
(`branch-misses`). They are C, in the one executable, with tests, as the repository's rules ask; the scripts stay on the
host for the measurements recorded with them.
