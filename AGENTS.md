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
- `build.sh` invokes Clang and produces `build/compiler`. It currently uses
  `-O3 -march=native`, so the machine executing the build determines the host ISA.
- `src/buster/compiler/c_lexer_test.c` checks `c_tokenize` against the scalar
  lexer on generated inputs. `build.sh` builds it as `build/c_lexer_test`,
  always with `-O3`.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
./build/compiler
./build/c_lexer_test
./build/c_lexer_test --cplusplus
```

## Lexer tests

`build/c_lexer_test` needs no corpus. It ports the snippet and random-input
checks of `c/c_tokenizer_test.c` in Accelerated-Zig-Parser, where the lexer
comes from, and compares `c_tokenize` with the scalar lexer token by token on:

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
  a read outside the source faults; the program then names the input and exits
  with status 1. Length 0 at a page end points at the guard page itself. The
  vectorized path loads aligned 64-byte lanes, so only a lane wholly outside
  the source can fault; an over-read within the source's last lane stays on a
  readable page and is not caught.

`--cplusplus` tokenizes everything as C++, with the C++ snippet and pieces. The
program prints each check's counts and first differences, and exits with status
1 if anything fails. The 9700X job runs both modes after the build. On that
machine the C and C++ runs take about 2.1 s and 2.3 s of CPU time. For a longer
run:

```sh
./build/c_lexer_test --cplusplus --seed 7 --fuzz 2000000
```
