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
measured end-to-end performance improvements. Do not import Buster's portability
or infrastructure machinery merely to match its style.

## Current repository and exact smoke commands

- `src/buster/compiler/main.c` is the executable entry point
- `build.sh` invokes Clang and produces **only** `build/compiler`, using
  `-march=native`, so the machine executing the build determines the host ISA.
  The benchmark is linked into this executable rather than built separately.
- `src/buster/compiler/c_lexer_test.c` holds the lexer tests. `main.c` includes
  it, and `./build/compiler test-lexer` runs them.
- `src/buster/compiler/c_lexer_benchmark.c` provides the `benchmark-lexer`
  command. The 9700X CI workflow runs it on the repository's C sources by default.
- `src/buster/compiler/x86_64_encoder.c` is the x86-64 encoder (the form table
  of legacy, VEX and EVEX forms, the scalar reference and the AVX-512 encoder,
  and the fixup interface: `BUSTER_X86_FORM_FIXUP` marks an instruction with a
  symbolic operand, `x86_encode` reports each marked instruction's end, and
  `x86_fixup_distance` gives its fields' offsets and addends from that);
  `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
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
  suffix from the repeated snippet and from four strings of random inputs
  (which continue the random sequence), placed to end exactly at the end of a
  readable page and to start exactly at the start of one, between `PROT_NONE`
  guard pages: 22,000 inputs. These also compare `c_tokenize_vectorized`, and
  a read outside the source faults; the command then names the input and exits
  with status 1. Length 0 at a page end points at the guard page itself. The
  vectorized path loads aligned 64-byte lanes, so only a lane wholly outside
  the source can fault; an over-read within the source's last lane stays on a
  readable page and is not caught.

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

`./build/compiler test-x86-encoder` (about 0.6 s) checks that every form fits a
template, both encoders on 452 known encodings from GNU as (each also with the
fixup flag, whose end they must report), a digest of the scalar encoder's output
for 100,000 random instructions, every batch width and variant of `x86_encode`
against the scalar encoder (bytes, offsets and fixup ends, in each of the four
loops: with neither, either or both of offsets and fixup ends) on every count
from 0 to 199 and on 300,000 random instructions of every form (legacy with x87
and MMX, VEX and EVEX, with opmasks, broadcasts, rounding, registers 16-31, FS
and GS segment overrides, and one in eight marked with the fixup flag), the same
between `PROT_NONE` guard pages, and that invalid operands are rejected
(immediates and rel8s outside their fields' ranges by `x86_validate` alone:
`x86_encode` writes their low bytes). `--count N` and `--seed N` change the
random streams.

The digest was recorded when GNU as encoded the same stream to the same bytes.
After changing the form table, the scalar encoder or the fixup interface,
cross-check with GNU as (binutils 2.47 for the recorded digest), then record the
digest the test prints:

```sh
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 > build/x86.s
as build/x86.s -o build/x86.o
./build/compiler test-x86-encoder --compare-object build/x86.o 400000 --seed 1
```

(`build.sh` deletes `build/`, so run these after building.) The printed
assembly uses only operands GNU as encodes with the same form
(`{load}`/`{store}`, `{vex}` and `{evex}`, immediates outside the imm8 range for
imm32 forms, and so on), so any difference is a bug in one of them. A quarter of the fields that can
be symbolic (32-bit displacements, rel32s, `loop`'s and `jrcxz`'s rel8s, imm32s
and 64-bit immediates) are printed as symbols plus their values; GNU as writes
zeros and a relocation for them, and `--compare-object` reads `.text`,
`.lengths` and `.rela.text` from its object and compares the bytes (with those
fields zeroed where the reported fixup ends put them) and each relocation's
offset, type, addend and symbol with the ones the fixup ends and
`x86_fixup_distance` give. Cross-check more than one seed after changing forms
(1.7 million instructions over seeds 1, 7 and 99 were last checked, with 195,000
relocations).

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
store alternatives that lost, and `x86_encode_64_offsets_then_pick`, the other
way to have fixup ends (every offset, then the caller picks the marked ones).
Decide them, and any other performance question, with measurements on the 9700X
(not Zen 4, and not llvm-mca, whose predictions are well off), using uops.info's
Zen 5 port data to explain them;
`docs/x86-64-encoder.md` has the current numbers. Clang's code for the loop
changes a lot with small source changes, so compare every width after changing
the encoder, and profile which loop the samples land in. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the lexer's, its
measurements are informational.
