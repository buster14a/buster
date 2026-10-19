# buster-fast

This file is the shared repository guide for coding agents. `CLAUDE.md` imports
it; keep project instructions here rather than maintaining parallel copies.

## Goal and scope

Build a new C compiler whose **implementation runs on Linux/x86-64, tuned for
AMD Zen 5**, with heavy, deliberate use of SIMD intrinsics. Optimize both
single-threaded compile throughput and useful multicore scaling while preserving
C correctness and generated-program quality.

This is a fresh compiler, not a compatibility branch of `buster14a/buster`.
Do not inherit that repository's frameworks, historical token parsers, CI
matrices, benchmark services, or acceptance bureaucracy. Reuse an idea only when
it earns its place here. Do not copy implementation code without checking its
license; do not select or change this repository's license on the owner's behalf.

The Zen 5 requirement describes the **host compiler executable**, not necessarily
the instruction set of every program it generates. Keep host ISA, generated-code
ISA, and output ABI separate. Do not add other host operating systems, older-CPU
production fallbacks, or hot-path CPU dispatch. Broader output targets and extra
applications are outside the current scope.

## Coding standards

Follow [the C coding style](docs/coding-style.md) for new or modified compiler
code. The essentials are: C and Clang on Linux/Zen 5, 4-space Allman formatting,
`snake_case` functions, `PascalCase` types, `BUSTER_` macros, one return per
function, direct and explicit control flow, bounded slices, arena-oriented
allocation, and safe SIMD memory accesses. Prefer simple data layouts and
measured end-to-end performance improvements. Prefer single-line statements,
declarations, and function headers where readable, and one aggregate field or
initializer entry per line. Do not import Buster's portability
or infrastructure machinery merely to match its style.

## Current repository and exact smoke commands

- `src/buster/compiler/main.c` is the executable entry point
- `build.sh` invokes Clang and produces **only** `build/compiler`, using
  `-march=native`, so the machine executing the build determines the host ISA.
  The benchmark is linked into this executable rather than built separately.
- `src/buster/compiler/c_lexer_test.c` holds the lexer tests. `main.c` includes
  it, and `./build/compiler test-lexer` runs them.
- `src/buster/compiler/c_lexer_benchmark.c` provides the `benchmark-lexer`
  command. The 9700X CI workflow runs it on the repository's C sources by
  default. `docs/c-lexer.md` records the GFNI classifier, the leading-comment
  skip, branch-free chunk state, the pipelined write-out, table-driven
  punctuator kinds, the delimiter scan, chunks read in place, the raw string
  check and their 9700X measurements.
- `src/buster/compiler/x86_64_encoder.c` is the x86-64 encoder (the form table
  of legacy, VEX and EVEX forms, the scalar reference and the AVX-512 encoder,
  and the fixup interface: `BUSTER_X86_FORM_FIXUP` marks an instruction with a
  symbolic operand, `x86_encode` reports each marked instruction's end, and
  `x86_fixup_distance` gives its fields' offsets and addends from that; and
  `x86_encode_address32` and `x86_encode_apx`, the same for 32-bit addresses and
  for APX's general registers 16 to 31 (REX2, and EVEX's B4 and X4), each with
  templates of its own);
  `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
- `docs/object-writer-and-linker.md` is the design of the object writer and the
  linker, with the 9700X measurements of mold, wild, lld and the kernel's file
  costs behind it, and the order to build it in. Its first step, the lane base
  layer below, is implemented; the rest isn't yet.
- `src/buster/compiler/lane.c` is the lane base layer that every phase of the
  object writer and the linker runs on (groups of threads that run the same
  code, their barrier and broadcast, `lane_range`, and arenas).
  `lane_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-lanes` runs them) and `lane_benchmark.c` provides `benchmark-lanes`.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
./build/compiler
./build/compiler test-lexer
./build/compiler test-lexer --cplusplus
./build/compiler benchmark-lexer src
./build/compiler test-x86-encoder
./build/compiler benchmark-x86-encoder
./build/compiler test-lanes
./build/compiler benchmark-lanes
```

## Sharing the 9700X

Agent sessions and CI share the 9700X: the buster14a org's self-hosted runner
runs on the same machine. Its timings only mean something when one thing runs
there at a time, so everything takes one lock:

- Run every build, test, benchmark and perf run there through
  `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`.
- The runner's job hooks take the same lock for every CI job, and a waiting CI
  job goes first.
- Hold the lock per run, never around a series of runs: a CI job waits for it
  and times out after 10 minutes.
- `~/buster-fast-bench/README-9700x.md` on the host describes the setup and how
  to undo it.

## Agent memory

Agents keep what they learn about this project in the owner's private
`davidgmbb/llm-memory` repository, cloned at `~/llm-memory` on each of the
owner's computers:

- Claude Code's memory folder for this project is a symlink to
  `~/llm-memory/projects/buster-fast/`, so Claude saves its memories there as
  usual.
- Any other agent reads `~/llm-memory/projects/buster-fast/MEMORY.md` before
  starting and saves what later sessions will need there, in the same format:
  one fact per file, indexed in `MEMORY.md`. `~/llm-memory/AGENTS.md` has the
  rules, and Codex's automatic memories are copied to `~/llm-memory/codex/`.
- Don't save what this repository already records, and never save secrets.
- After saving, run `~/llm-memory/bin/llm-memory sync` to share the memory with
  the owner's other computers.

Where `~/llm-memory` doesn't exist, skip this.

## Lexer tests

`./build/compiler test-lexer` needs no corpus. Its checks are ports of the
snippet and random-input checks of `c/c_tokenizer_test.c` in
Accelerated-Zig-Parser, where the lexer comes from. They compare `c_tokenize`
with the scalar lexer token by token on:

- the boundary snippet after 0 to 1,099 bytes of padding at each of the 64
  misalignments from a cache line: 70,400 placements, none of which may need
  the scalar lexer;
- 200,000 random inputs made of weighted C pieces. `--fuzz N` sets how many,
  and `--seed N` picks another sequence (the same inputs as upstream's
  `--seed N`). The first inputs that differ are saved as `fuzz_failure_N.c` in
  the current directory;
- page edges: every length from 0 to 1,099 bytes, cut as a prefix and as a
  suffix from the repeated snippet, four strings of random inputs (which
  continue the random sequence) and three comment templates, placed to end
  exactly at the end of a readable page and to start exactly at the start of
  one, between `PROT_NONE` guard pages: 35,200 inputs. These also compare
  `c_tokenize_vectorized`, and a read outside the source faults; the command
  then names the input and exits with status 1. Length 0 at a page end points
  at the guard page itself. The chunk loop loads aligned 64-byte lanes, so
  only a lane wholly outside the source can fault; an over-read within the
  source's last lane stays on a readable page and is not caught. The
  leading-comment scan's loads are unaligned and masked to the source, so the
  guard pages check them fully;
- 7,040 comment cases across every source misalignment: closers around the
  leading-comment scan's 64-byte loads and around 512-byte chunk boundaries,
  including closers whose `*` ends a chunk (also a chunk wholly inside the
  comment), a spliced closer, a `/*/` whose `*/` ends the comment, and a
  comment inside a preprocessing directive.

`--cplusplus` tokenizes everything as C++, with the C++ snippet and pieces. The
command prints each check's counts and first differences, and exits with status
1 if anything fails. The 9700X job runs both modes after the build. On that
machine, with `build.sh`'s unoptimized build, the C and C++ runs take about
5.7 s and 6.1 s. For a longer run:

```sh
./build/compiler test-lexer --cplusplus --seed 7 --fuzz 2000000
```

## Lexer benchmarks

`./build/compiler benchmark-lexer [--cplusplus] path...` checks each matching
source against the scalar lexer and reports 21 interleaved-pass median
throughput, cycles per byte, instructions per byte and IPC (hardware counters
are zero when unavailable). Paths can be files or directories; `--cplusplus`
selects C++ source/header extensions. The default 9700X job runs
`benchmark-lexer src` after the tests, pinned to one allowed CPU. Measurements
are informational: correctness failures fail CI, but throughput values are not
yet compared against a performance baseline. Keep just one compiler executable.

## x86-64 encoder tests

`./build/compiler test-x86-encoder` (about 1 s) checks that every form fits a
template, both encoders on 761 known encodings from GNU as (each also with the
fixup flag, whose end they must report), a digest of the scalar encoder's output
for 100,000 random instructions, every batch width and variant of `x86_encode`
against the scalar encoder (bytes, offsets and fixup ends, in each of the four
loops: with neither, either or both of offsets and fixup ends) on every count
from 0 to 199 and on 300,000 random instructions of every form (legacy with x87
and MMX, VEX and EVEX, with opmasks, broadcasts, rounding, registers 16-31,
gathers and scatters, FS and GS segment overrides, and one in eight marked with
the fixup flag), the same between `PROT_NONE` guard pages, and that invalid
operands are rejected (immediates and rel8s outside their fields' ranges by
`x86_validate` alone: `x86_encode` writes their low bytes; and memory operands
in the 232 FS and GS forms without room for the override). Then it runs those
checks again with 32-bit addresses, but the known fixups: 34 known encodings, a
digest of their own, `x86_encode_64_address32` against the scalar encoder on the
random streams and at page edges, and invalid operands, among them MPX memory
operands and memory operands in the 312 FS and GS forms and the 116 forms
without room for the `67`. And again for APX, but the known fixups: 153 known
encodings, 31 known invalid instructions (as GNU as rejects them), a digest of
their own, `x86_encode_64_apx` against the scalar encoder on random streams in
which half the forms have REX2 and a quarter are EVEX ones, and at page edges,
and invalid operands, among them a high byte register with REX2, registers 16 to
31 in forms without REX2 or EVEX and XMM16 to XMM31 in SSE's forms.
`--address32` and `--apx` run only their pass, and `--count N` and `--seed N`
change the random streams.

The digests were recorded when GNU as encoded the same streams to the same
bytes. After changing the form table, the scalar encoder or the fixup interface,
cross-check with GNU as (binutils 2.47 for the recorded digests) for every entry
point, then record the digests the test prints:

```sh
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 > build/x86.s
as build/x86.s -o build/x86.o
./build/compiler test-x86-encoder --compare-object build/x86.o 400000 --seed 1
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --address32 > build/x86_32.s
as build/x86_32.s -o build/x86_32.o
./build/compiler test-x86-encoder --compare-object build/x86_32.o 400000 --seed 1 --address32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --apx > build/x86_apx.s
as build/x86_apx.s -o build/x86_apx.o
./build/compiler test-x86-encoder --compare-object build/x86_apx.o 400000 --seed 1 --apx
```

(`build.sh` deletes `build/`, so run these after building.) The printed assembly
uses only operands GNU as encodes with the same form (`{load}`/`{store}`,
`{vex}` and `{evex}`, immediates outside the imm8 range for imm32 forms, `{nf}`
for APX's NF forms, `{dfv=...}` for `ccmp`'s and `ctest`'s default flags value,
and so on), so any difference is a bug in one of them. A quarter of the fields
that can be symbolic (32-bit displacements, rel32s, `loop`'s and `jrcxz`'s
rel8s, imm32s and 64-bit immediates) are printed as symbols plus their values;
GNU as writes zeros and a relocation for them, and `--compare-object` reads
`.text`, `.lengths` and `.rela.text` from its object and compares the bytes
(with those fields zeroed where the reported fixup ends put them) and each
relocation's offset, type, addend and symbol with the ones the fixup ends and
`x86_fixup_distance` give. With `--address32`, the printer adds `addr32` where
no general register gives the address size, and the relocations of displacements
other than RIP-relative ones are R_X86_64_32; with `--apx`, it names registers
16 to 31 `r16` to `r31` (`r16d`, `r16w`, `r16b`). Cross-check more than one seed
after changing forms (1.7 million instructions over seeds 1, 7 and 99 were last
checked for each entry point, with 198,573, 197,926 and 197,735 relocations).

## x86-64 encoder benchmark

`./build/compiler benchmark-x86-encoder [--count N] [--section N] [--uniform |
--vector] [--offsets] [--fixups] [--only NAME]` checks every implementation
against the scalar loop, then reports 21 interleaved-pass medians per encoded
instruction (time, cycles, instructions, IPC). The stream is a compiler-like mix
of 16,384 legacy instructions by default (comparable with older measurements), a
mix like vectorized code (AVX-512, AVX2, BMI) with `--vector`, or random
instructions of every form with `--uniform`; `--section N` encodes it N
instructions per call, `--offsets` stores every instruction's offset, and
`--fixups` marks the calls and RIP-relative operands and reports their ends.
The variants are the batch widths (`x86_encode_4` to `x86_encode_64`,
instructions per loop iteration), the template gather, output store and fixup
store alternatives that lost, `x86_encode_64_offsets_then_pick`, the other
way to have fixup ends (every offset, then the caller picks the marked ones),
and `x86_encode_64_apx`, `x86_encode_apx`'s loop (the streams have no registers
16 to 31, so its bytes are the same).
Decide them, and any other performance question, with measurements on the 9700X
(not Zen 4, and not llvm-mca, whose predictions are well off), using uops.info's
Zen 5 port data to explain them;
`docs/x86-64-encoder.md` has the current numbers. Clang's code for the loop
changes a lot with small source changes, so compare every width after changing
the encoder, and profile which loop the samples land in. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the lexer's, its
measurements are informational.

## Lane tests

`./build/compiler test-lanes` (about 0.2 s) checks the lane base layer. The
arena checks run on the calling thread, and every other check runs on groups of
1, 2, 3 and 8 lanes and compares what the lanes produce with a sequential
reference:

- arenas: pushes at every power-of-two alignment up to 2 MB, invalid
  alignments, a push one byte past the capacity (it fails and leaves the arena
  as it was), sizes and counts whose arithmetic overflows, and the page sizes
  asked for, read back from `/proc/self/smaps`;
- `lane_range` and `lane_range_by_cost` for every count from 0 to 1,000, with
  seven cost patterns: the parts must cover the items in lane order, and each
  item must go to the lane whose share of the total cost holds its midpoint;
  and the same of `range_part` and `range_part_by_cost` with 64, 512 and 1,001
  parts;
- 10,000 rounds of a sync and a broadcast, with a lane arriving late every
  1,000 rounds, by turns so late that the others sleep on the futex and only
  so late that they wait in `mwaitx`, then as many broadcasts back to back,
  and narrow tasks spread by `lane_from_task_index`;
- a lane that makes itself a group of one and rejoins, and the scratch arenas;
- the design's exchange (rule 4 in "Lanes"): records counted per hash
  partition, placed, filled and resolved by partition, byte-identical to the
  reference for every lane count;
- parts (rules 1 and 3): 12 inputs cut by `lane_parts_by_cost`, whose parts the
  lanes take with `lane_take`, then count, place and fill by part, with a late
  lane in every loop. The output must be byte-identical to the reference,
  every part must be taken once, and every lane must get the same order,
  costliest first.

A watchdog ends the run with status 1 if a group doesn't finish within 60 s,
which is how a lost wake-up would show. `--rounds N` and `--seed N` change the
rounds and the random inputs.

## Lane benchmark

`./build/compiler benchmark-lanes [--lanes N] [--cpus LIST] [--rounds N]` pins
lane i of groups of 1 to 8 lanes to the i-th CPU of `--cpus` (by default the
allowed CPUs in order: 0 to 7 on the 9700X, one per core) and reports
21-pass medians of a sync and a broadcast when every lane arrives at once, of
`pthread_barrier_wait` (the RAD Debugger's barrier on Linux) in the same loop,
of waking lanes that wait in `mwaitx` and that sleep on the futex, and of
starting a group. Then, on the largest group, how much slower lanes run after
waiting 0.2 to 5 ms for a lane that works alone, and how well rule 1's parts
and the static ranges balance three phases, with every lane at full speed and
with half of them at half speed. Then what an arena's reservation and first
touches cost. Run it under the 9700X's lock
without `taskset`, since it pins its lanes itself.
`docs/object-writer-and-linker.md` ("Lanes") has the numbers. The default 9700X
job runs it after the tests; like the others, its measurements are
informational.
