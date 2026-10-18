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
- `src/buster/compiler/c_lexer_benchmark.c` checks `c_tokenize` against the
  scalar lexer on a corpus of sources and times both. `build.sh` builds it as
  `build/c_lexer_benchmark`, always with `-O3`.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
./build/compiler
```

## Lexer benchmark

`build/c_lexer_benchmark` takes files and directories of C sources (`.c`, `.h`)
or, with `--cplusplus`, C++ sources. It prints the corpus check and benchmark
lines of `c/c_tokenizer_test.c` in Accelerated-Zig-Parser, where the lexer comes
from: medians of 21 interleaved passes in ms, GB/s, cycles/B and
instructions/B. Pin it to one CPU:

```sh
taskset -c 2 ./build/c_lexer_benchmark <zig-0.14.1>/lib/libc
taskset -c 2 ./build/c_lexer_benchmark --cplusplus <c++ header directories>...
```

At the import (upstream commit f4482e2), on the 9700X, `c_tokenize` ran at
about 1.21 cycles/B (4.55 GB/s) on Zig 0.14.1's `lib/libc` and 1.38 cycles/B
(4.0 GB/s) on the libc++, libc++abi, libstdc++, LLVM, clang and Qt headers, the
same as upstream. Compare cycles/B and instructions/B rather than GB/s across
machines.
