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

## Licensing and contributions

Read [LICENSING.md](LICENSING.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [CONTRIBUTING.md](CONTRIBUTING.md) before importing or submitting material.
Original contributions use `Apache-2.0 WITH LLVM-exception`; identified
components and local changes to their portions follow their retained terms.
Do not add the LLVM exception over Apache-only upstream material or label a
mixed-source file as wholly first-party.

Record the sources and revisions of imports, adaptations and generated data,
including generator inputs, and preserve copyright, license, attribution and
modification notices. Changes to the license remain the owner's decision.
Follow the prospective DCO policy and its in-flight-PR transition. Never
fabricate sign-offs, treat agent co-author trailers as human certification, or
retroactively certify historical commits.

## Host and output scope

The Zen 5 requirement describes the **host compiler executable**, not necessarily
the instruction set of every program it generates. Keep host ISA, generated-code
ISA, and output ABI separate. Do not add other host operating systems, older-CPU
production fallbacks, or hot-path CPU dispatch. Broader output targets and extra
applications are outside the current scope; but the x86 encoder encodes 16- and
32-bit modes' instructions too, as the owner asked.

## Issue ownership

Every issue being investigated, tackled or fixed must be claimed in the issue
itself before work begins. Read its latest comments and linked pull requests,
then post a comment identifying who is taking the work and the scope being
claimed. A local plan, branch, pull request or assignee alone does not replace
this comment. When work covers several issues, claim each one.

Respect existing claims: coordinate with the current owner before taking over
or starting overlapping work. Keep the claim current with the branch or pull
request, and comment on the issue when handing off or stopping so others know
whether the work is still owned.

## Coding standards

Write code in **C** unless the goal or action explicitly disallows C or requires
another language. This applies to supporting tools, tests, and automation as
well as compiler code.

Follow [the C coding style](docs/coding-style.md) for new or modified compiler
code. The essentials are: C and Clang on Linux/Zen 5, 4-space Allman formatting,
`snake_case` functions, `PascalCase` types, `BUSTER_` macros, one return per
function, direct and explicit control flow, bounded slices, arena-oriented
allocation, and safe SIMD memory accesses. Prefer simple data layouts and
measured end-to-end performance improvements. Prefer single-line statements,
declarations, and function headers where readable, and one aggregate field or
initializer entry per line. Do not import Buster's portability
or infrastructure machinery merely to match its style.

## Data-parallel first

Design every stage and every pull request data-parallel, vectorized and
branch-free first, as `src/buster/compiler/c_lexer.c` and `x86_64_encoder.c`
are built. The tools include masks over many items, prefix sums, in-register
permutes as table lookups, compress and expand, sorting and segmented scans,
bitsets, nearest-smaller-values and pointer jumping. Each vector path has an
exact scalar path for the rare cases, which is also its twin and test oracle:
both must give identical output or, where the two algorithms legitimately
differ, pass a semantic check.

No stage is assumed inherently sequential. Where C fixes an order (macro state,
typedef visibility), vectorize within each step and keep the ordered part small.
A part that stays scalar says why with measurements on the 9700X, as "SIMD: what
pays" in `docs/object-writer-and-linker.md` does. The owner set this rule on
2026-10-20, replacing the premise of
[#15](https://github.com/buster14a/buster-fast/issues/15) that the parser and
the type checker are inherently sequential. The issues that apply it are listed
in the "Data-parallel first" section of
[#115](https://github.com/buster14a/buster-fast/issues/115). "SIMD and Zen 5" in
[the C coding style](docs/coding-style.md) has the Zen 5 costs that shape these
designs and the compilers already built this way.

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
  templates of its own; and `x86_encode_mode32` and `x86_encode_mode16` (and
  `x86_encode_scalar_32` and `x86_encode_scalar_16`), the same for 32-bit and
  16-bit modes, with their operand and address sizes, segment overrides and own
  instructions, and templates of their own);
  `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
- `docs/object-writer-and-linker.md` is the design of the object writer and the
  linker, with the 9700X measurements of mold, wild, lld and the kernel's file
  costs behind it, and the order to build it in. The lane base layer, the
  encoder's fixups, the representation, the object writer and the direct
  path's first executables below are implemented; the rest of the linker
  isn't yet.
- `src/buster/compiler/lane.c` is the lane base layer that every phase of the
  object writer and the linker runs on (groups of threads that run the same
  code, their barrier and broadcast, `lane_range`, and arenas).
  `lane_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-lanes` runs them) and `lane_benchmark.c` provides `benchmark-lanes`.
- `src/buster/compiler/link.c` is the representation the object writer and the
  linker share: names, atoms, symbols and fixups in column arrays indexed by
  32-bit ids, in arenas, and `link_add_code_fixups`, which turns the encoder's
  marked instructions into fixups of the design's kinds. `link_program.c`
  generates test programs in it (static ones without libc, and ones that call
  libc, from a seed and sizes) for the object writer's and the linker's tests
  and benchmarks. `link_test.c` holds the tests of both (`main.c` includes
  them; `./build/compiler test-link` runs them).
- `src/buster/compiler/object_writer.c` is the object writer: an ELF
  relocatable object per unit of the representation (`object_prepare` once per
  link, then `object_build` per object, `object_write_file`, and
  `object_write_units` for a group of lanes). `object_writer_test.c` holds its
  tests (`main.c` includes it; `./build/compiler test-object-writer` runs them)
  and `object_writer_benchmark.c` provides `benchmark-object-writer`.
- `src/buster/compiler/executable.c` writes the direct path's first
  executables: a link in that representation laid out, patched and written as
  a static executable without libc (step 4 of the design's plan), with a build
  ID, by every lane of a group. `executable_test.c` holds its tests (`main.c`
  includes it; `./build/compiler test-executable` runs them) and
  `executable_benchmark.c` provides `benchmark-executable`.
- `src/buster/compiler/call_graph.c` is the call graph check, the coding
  style's rule against recursion checked mechanically: `check-call-graph`
  reads the LLVM IR of the translation units `build.sh` compiles, builds their
  call graph and reports every cycle (Tarjan's algorithm, iterative) that isn't
  in its allowlist, `call_graph_allowed`. `./build.sh check-call-graph` writes
  that IR to a temporary directory and runs it. `call_graph_test.c` holds its
  tests (`main.c` includes both; `./build/compiler test-call-graph` runs them),
  and `call_graph_capture/` the C fixture that checks the IR capture itself.
- `src/buster/compiler/c_intern.c` is the identifier intern table (an
  `IdentId` per distinct spelling) and the predefined names that get the first
  ids (keywords, directive names, the preprocessor's names), and
  `src/buster/compiler/c_refine.c` is token refinement: per file, after the
  lexer, each identifier's `IdentId` (in place of its end) and each token's
  exact leading-space bit. `docs/c-refinement.md` explains the design and its
  9700X measurements. `c_refine_test.c` holds their tests (`main.c` includes
  it; `./build/compiler test-refinement` runs them) and `c_refine_benchmark.c`
  provides `benchmark-refinement`.
- `src/buster/compiler/c_preprocessor.c` is the preprocessor: one translation
  unit's token stream from its files' refined tokens, and `./build/compiler -E`
  (as `clang -E`). `docs/c-preprocessor.md` explains the design, the `clang -E`
  oracle and the 9700X measurements. `c_preprocessor_test.c` holds its tests
  (`main.c` includes it; `./build/compiler test-preprocessor` runs them) and
  `c_preprocessor_benchmark.c` provides `benchmark-preprocessor`.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
./build/compiler
./build/compiler test-call-graph
./build.sh check-call-graph
./build/compiler test-lexer
./build/compiler test-lexer --cplusplus
./build/compiler benchmark-lexer src
./build/compiler test-x86-encoder
./build/compiler benchmark-x86-encoder
./build/compiler test-lanes
./build/compiler benchmark-lanes
./build/compiler test-link
./build/compiler test-object-writer --cross-check
./build/compiler benchmark-object-writer
./build/compiler test-executable
./build/compiler benchmark-executable
./build/compiler test-refinement src
./build/compiler benchmark-refinement src
./build/compiler test-preprocessor --clang
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
./build/compiler -E -march=native -Isrc --predefines build/predefines.h -o build/main.i src/buster/compiler/main.c
./build/compiler benchmark-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
```

The compiler's sources need `-march=native`'s macros (`c_lexer.c` stops with
`#error` without AVX-512's), which the preprocessor's built-in predefines don't
have: `--predefines` gives it clang's for those options.

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

## Call graph check

The coding style forbids recursion over input-dependent structures, and
`./build.sh check-call-graph` checks it on every function of the compiler. It
writes the LLVM IR of `build.sh`'s translation units, with the same flags and
optimization level, `-O3`, but `-Xclang -disable-llvm-passes` (no pass, not
even the always-inliner, so a call in the IR is a call in the source, while
the IR holds every body the build may inline: at `-O0`, clang doesn't emit an
`extern inline` body at all) into a temporary directory, and runs
`./build/compiler check-call-graph FILE.ll...` on them (so it needs a build).
First it captures `src/buster/compiler/call_graph_capture` the same way, a
cycle through an `extern inline` body beside a strong definition in another
unit, and fails unless the check reports it.
The command reads each `define` and the direct `call`, `invoke` and `callbr`
instructions in its body; a callee is its translation unit's function or alias
of that name first (static functions are per unit: the benchmarks' copies of
`lane.c` are separate functions), then the external symbol's strong
definitions, or all its weak ones when it has none (the linker takes one of
them by link order), and every definition of the name in the calling unit,
which clang may inline into the call whatever the linker takes (an
`available_externally` or `*_odr` body, `always_inline`, `flatten`). A
replaced weak body that clang wouldn't inline then counts too: that can only
add a cycle, never hide one; an alias, through any chain of them, is its target's
definition in its own module, whatever replaces that name elsewhere. A second strong definition of a name fails the check. Calls of functions defined
nowhere (libc, intrinsics) and calls through pointers, which the style allows
only at the thread-entry boundary, are left out; a call of an ifunc or of an
alias that leads to no definition fails the check.

It reports each strongly connected component of more than one function and
each function that calls itself, with the shortest call path through the
component's first function, as `unit.ll:function -> ... -> unit.ll:function`,
then a summary line, and exits with status 1 on a cycle not in
`call_graph_allowed`, on an allowlist entry that matches no cycle, and on IR it
can't read. An entry names a component's functions, each as many times as
the component has a function of that name; add one only for the
style's "trivial statically bounded recursion", with a comment saying what
bounds its depth. The list is empty: the tree has no cycle. The 9700X job runs
`test-call-graph` and the check after the build; the IR takes about 1.5 s.

`./build/compiler test-call-graph` (about 0.4 s) checks the report, exactly,
on IR of the call forms clang writes and of what must be left out, a
self-call, a two-function cycle, a cycle with a chord (whose path must be the
shortest), a cycle through three translation units beside static and external
functions of the same name, clang's IR of recursion through aliases and of a
call of an ifunc, aliases that lead nowhere, clang's IR of weak and strong
functions and aliases of one name, of an alias of a replaced weak function, and
of `available_externally`, `always_inline` and `flatten` inlining past a
strong definition elsewhere, in both orders, replaced weak bodies, two units' copies of a cycle
allowed by one entry, a cycle grown past its entry with an unused entry, a
cycle through two units' static functions of the same name that entries naming
it once mustn't match, malformed IR, and a chain of a million functions and a
ring of 100,000, deeper than a recursive walk's stack.

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

`./build/compiler test-x86-encoder` (about 1.5 s) checks that every form fits a
template (and that building each, in every mode, at a `PROT_NONE` page writes
nothing past it), both encoders on 761 known encodings from GNU as (each also
with the fixup flag, whose end they must report), a digest of the scalar
encoder's output
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
31 in forms without REX2 or EVEX and XMM16 to XMM31 in SSE's forms. And in
32-bit and 16-bit modes: 159 and 150 known encodings, 13 and 16 known fixups, a
digest of each, `x86_encode_64_mode32` and `x86_encode_64_mode16` against the
scalar encoders on random streams with both address sizes and segment
overrides in the decorations, and at page edges, streams of instructions longer
than 15 bytes at a page end (16-bit mode has them), which the loop must reject
without writing past the output's capacity, and invalid operands, among them
registers that would need REX, 16-bit addresses that aren't ModRM's, forms of
64-bit mode alone, instructions past 15 bytes and segment overrides where an
instruction takes none. `--address32`, `--apx`, `--mode32` and
`--mode16` run only their pass, and `--count N` and `--seed N` change the random
streams.

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
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode32 > build/x86_mode32.s
as build/x86_mode32.s -o build/x86_mode32.o
./build/compiler test-x86-encoder --compare-object build/x86_mode32.o 400000 --seed 1 --mode32
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --mode16 > build/x86_mode16.s
as build/x86_mode16.s -o build/x86_mode16.o
./build/compiler test-x86-encoder --compare-object build/x86_mode16.o 400000 --seed 1 --mode16
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
`x86_fixup_distance` give, and the mode's vectorized encoder's bytes and fixup
ends with the scalar encoder's. With `--address32`, the printer adds `addr32`
where no general register gives the address size, and the relocations of
displacements other than RIP-relative ones are R_X86_64_32; with `--apx`, it
names registers 16 to 31 `r16` to `r31` (`r16d`, `r16w`, `r16b`); and with
`--mode32` and `--mode16`, the printed assembly is in `.code32` or `.code16`,
and the printer writes the mode's mnemonics and prefixes (`addr16`, `data32`,
`jcxz`, `lcall`, segment overrides as prefixes, which GNU as keeps on a default
segment, and `.arch i386` around a test register's move), whose relocations are
never PLT32 or 32S. Cross-check more than one seed after changing forms (1.7
million instructions over seeds 1, 7 and 99 were last checked for each 64-bit
entry point, with 198,573, 197,926 and 197,735 relocations, and 1.2 million for
each of 32-bit and 16-bit mode, with 135,563 and 136,322).

## x86-64 encoder benchmark

`./build/compiler benchmark-x86-encoder [--count N] [--section N] [--uniform |
--vector] [--mode32 | --mode16] [--offsets] [--fixups] [--only NAME]` checks
every implementation against the scalar loop, then reports 21 interleaved-pass
medians per encoded instruction (time, cycles, instructions, IPC). The stream is
a compiler-like mix of 16,384 legacy instructions by default (comparable with
older measurements), a mix like vectorized code (AVX-512, AVX2, BMI) with
`--vector`, or random instructions of every form with `--uniform`; `--section N`
encodes it N instructions per call, `--offsets` stores every instruction's
offset, and `--fixups` marks the calls and RIP-relative operands and reports
their ends. The variants are the batch widths (`x86_encode_4` to
`x86_encode_64`, instructions per loop iteration), the template gather, output
store and fixup store alternatives that lost, `x86_encode_64_offsets_then_pick`,
the other way to have fixup ends (every offset, then the caller picks the marked
ones), and `x86_encode_64_apx`, `x86_encode_apx`'s loop (the streams have no
registers 16 to 31, so its bytes are the same). `--mode32` and `--mode16` time
`x86_encode_64_mode32` or `x86_encode_64_mode16` and the mode's scalar loop
instead, on a mix like 32-bit compiler output or like real-mode code (or with
`--uniform`, the mode's forms).
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

## Link representation tests

`./build/compiler test-link` checks `link.c` and `link_program.c`:

- random records of every table read back from the columns, with ids from 1
  and record 0 zero;
- every table at its capacity, the side table of wide addends full, a name
  arena too small, and arenas too small for the tables: each add fails with
  `LINK_ERROR_FULL` and changes nothing;
- each rule the adds and `link_check` enforce, with the error it gets;
- every fixup kind on the instructions it can patch, with the bytes, field
  offsets and addends GNU as gives (binutils 2.42), each alone and in streams
  of up to 1,000 marked instructions with unmarked ones between them, then
  every kind on fields it can't patch, and references that don't match the
  marked instructions;
- addends around int32's limits, which go to the side table exactly when they
  don't fit, through an instruction's distance to its end too;
- generated programs with seven sets of options: the defaults, the smallest,
  16 units, no absolute addresses, `--functions N` workers (2,000 by default)
  without `SIZE32` and `SIZE64`, which wild 0.10 doesn't link, and two that
  call libc (the defaults, and 16 units without sizes). The same seed must
  give byte-identical tables and bytes, and another seed others. Each is laid
  out in memory as a static executable at a fixed address (`link_test_load`,
  which patches every kind and is the reference for the direct path's patch
  kernel) and run in a child process. A program that calls libc gets
  stand-ins for `write`, `strlen`, `environ` and `__errno_location`, and the
  child does what the C runtime does around `main`. Each must exit with status
  0 having printed the output the generator's model predicts, and again with
  its TLS segment starting half its alignment past a multiple of it, as a
  linker may start `.tdata` when `.tbss` is more aligned. Every set with
  two thread-locals, an init function and a fini function must have every
  class and every kind its options allow, and a copy of the defaults' program
  with the hexadecimal digits' fixup one byte off must print something else.

`--seed N` changes the random records and programs. The programs check is
what scales: `--functions 1000000` builds three programs of a million workers
each.

`--print-assembly DIR` writes the same programs (for the same `--seed` and
`--functions`) as GNU as source instead: a directory for each set, with a
`unit_N.s` per unit and `expected`, the output. Each atom's bytes go in its
class's section, with a label at each symbol and a `.reloc` for each fixup, so
GNU as's object of a unit is a reference for the object writer's. To check
the programs with real toolchains, assemble every unit, then link each
program, run it and compare what it prints (the `static-*` directories with
`-static -e _start`, the `libc-*` ones through `cc`):

```sh
./build/compiler test-link --print-assembly build/programs
for s in build/programs/*/unit_*.s; do as $s -o ${s%.s}.o; done
for d in build/programs/static-*; do
    ld.bfd -static -e _start -o $d/program $d/unit_*.o && $d/program | cmp - $d/expected
done
for d in build/programs/libc-*; do
    cc -o $d/program $d/unit_*.o && $d/program | cmp - $d/expected
done
```

The same with `ld.lld`, `mold` or, for the `*-no-sizes` sets, `wild` for
`ld.bfd`, and with `-fuse-ld=lld`, `mold` or `wild` and `-no-pie` or `-static`
for `cc`. On the 9700X, GNU ld 2.47, lld 23.1.1 and mold 3.0.0 linked every
set and wild 0.10.0 the `*-no-sizes` ones, 37 links per seed, for seeds 1, 2,
3, 7, 99 and 1,000, and for seed 5 with 100,000 workers; every program printed
what its model predicts. GNU ld and lld (with `-z norelro`) also start the TLS
segment off its alignment when told where: seed 1's `static-defaults` with
`--section-start=.tdata=0x500008 --section-start=.tbss=0x500030`.

## Object writer tests

`./build/compiler test-object-writer [--seed N] [--cross-check] [--keep]` checks
`object_writer.c`:

- objects: each unit's object of `test-link`'s seven programs (with 500 workers
  in the one that takes `--functions`), of a link made by hand (gaps between
  atoms, an atom of no size, weak, hidden and protected symbols, pointers into
  a string pool, units without atoms between and after the others) and of a
  link without atoms, with and without function sections.
  Each is built twice, the second time over poisoned memory, and must come out
  the same; the test's own ELF reader reads it back and checks the rules every
  object keeps; and it is compared with the representation, from which the test
  works out every section, symbol and relocation again, with its own tables of
  section formats and relocation types. Among them, every local-exec TLS
  reference must be an `R_X86_64_TPOFF32` against its TLS symbol over a zero
  field, and every call through the GOT an `R_X86_64_GOTPCRELX`. Then an arena
  too small for an object (`OBJECT_ERROR_FULL`) and an addend past int64
  (`OBJECT_ERROR_RANGE`);
- many sections: one unit of 33,000 functions in sections of their own, 66,039
  sections: `e_shnum` and `e_shstrndx` in section 0, and symbols' indices
  through `SHN_XINDEX` and `.symtab_shndx`;
- lanes: every unit's object of a program of 24 units and a 25th without
  atoms written by `object_write_units` with groups of 1, 2, 3 and 8 lanes,
  byte-identical to `object_build`'s; and a group in which a lane has no room
  to start, which writes no object and gives every unit `OBJECT_ERROR_FULL`;
- files: `object_write_file` into a new file, over a larger and a smaller one
  (in place: the same inode, truncated), over one with another hard link (a
  new file with its mode, and the other link keeps the old bytes), also with a
  name of `NAME_MAX` bytes, into `/dev/null`, into a pipe, and into a missing
  directory (an error);
- with `--cross-check`, which needs `readelf`, `llvm-readobj`, `as`,
  `ld.bfd`, `ld.lld`, `mold` and `wild` on `PATH`, and a `cc` that takes
  `-fuse-ld=wild` (GCC 16 on the 9700X):
  - `readelf -W` and `llvm-readobj` list every object's sections, symbols and
    relocations, which must be what the test's reader read, field by field;
  - GNU ld, lld, mold and wild link each program's objects, without errors or
    warnings: the static programs with `-static -e _start`, and the ones that
    call libc through `cc` as a PIE, with `-no-pie` and with `-static`. The
    executable must exit with status 0 having printed what the program's model
    predicts. wild links only the programs without `SIZE32` and `SIZE64`, which
    wild 0.10 refuses, and among them one of 33,000 functions past
    `SHN_LORESERVE` sections;
  - GNU as assembles each unit as `test-link --print-assembly` writes it, and
    its object must match the writer's: the same sections and bytes, symbols
    and relocations. They may differ where GNU as does: it orders sections as
    the source names them and places them in the file its own way, keeps an
    empty `.text`, `.data` and `.bss`, adds a `.note.gnu.property` where
    binutils is built to (as on the 9700X), writes no `.comment` or
    `STT_FILE`, keeps only the unnamed symbols relocations into `SHF_MERGE`
    sections need, and for a `.reloc` against a named local symbol keeps the
    symbol where the writer, like GNU as for an instruction, names the
    section's. So a relocation's target is compared by where it points, but
    its form must be the same for the GOT, TLS and size types and into
    `SHF_MERGE` sections.

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. `--seed N` changes the programs. On the
9700X (binutils 2.47, LLVM 23.1.1, mold 3.0.0, wild 0.10.0 and GCC 16.2.1)
the checks take about 0.03 s, and 3.5 s with `--cross-check`.

## Object writer benchmark

`./build/compiler benchmark-object-writer [--units N] [--functions N]
[--filler N] [--function-sections] [--directory DIR] [--seed N]` generates a
program of `--units` units (64 by default), each of `--functions` workers (200)
of up to `--filler` filler instructions (40), with 8 globals, 2 thread-locals,
20 strings and an init and a fini function per unit: by default each unit is
about one of synth's translation units. It reports, per object, 21-pass medians
of `object_prepare` (once per link), of `object_build` alone (with cycles,
instructions and IPC), and of writing each object to a file in `--directory`
(`$TMPDIR` or `/tmp`) over the one from the pass before (in place) and as a new
file, each against its floor: the same system calls without the writer's
`fstat`. Each way writes files of its own and the ways take turns going first,
since rewriting pages just written costs less. Run it pinned to one CPU, and
with `--directory` on each file system to measure;
`docs/object-writer-and-linker.md` ("The object writer (`-c`)") has the numbers
for tmpfs and btrfs. The default 9700X job runs it after the tests, pinned to
one allowed CPU; like the others, its measurements are informational.

## Direct executable tests

`./build/compiler test-executable` (about 0.1 s on the 9700X) checks
`executable.c` in a directory of its own under `--directory` (`TMPDIR` or
`/tmp` by default), which it removes unless `--keep` says to leave it:

- the build ID's chunk hash: every length up to 2,100 bytes, cut at random
  places, hashes as it does whole, and zeros of each length up to a page hash
  apart from the next length's;
- test-link's program sets, and one of 50,000 workers in 38 chunks, each
  written by groups of 1, 2, 3 and 8 lanes, every time from zeroed fields over
  the last file scribbled over, so that a lane that skipped a field or a chunk
  shows. The files and build IDs must be byte-identical, and each program must
  run and print what its model predicts. In each file every fixup's field must
  hold its kind's value, the GOT must have a slot for each target of a GOT
  kind (numbered from 1 in symbol order, holding its value), the headers must
  have lld's layout, no chunk boundary may split a field, and the build ID
  must be the tree of the file's chunks. A copy of the defaults' program with
  one pointer 8 bytes off must print something else, and the two sets that
  call libc (step 5's) must be refused, an import named, without creating the
  output;
- chunk boundaries: programs with a call's rel32 0 to 4 bytes before the
  nominal 256 KB boundary and an `ABS64` 0 to 8 bytes before 512 KB, both in
  atoms larger than a chunk. A boundary must move to the end of a field that
  crosses it and stay where it is otherwise, with every lane count, and the
  program must run;
- the output file: created; overwritten in place by a smaller program (and
  truncated) and with its mode restored; written beside it and renamed over it
  when it has another link (which keeps its bytes), when it is a symbolic link,
  when a process runs it (which runs on), when it is someone else's (checked
  only when the test may `chown`, as root), past a file a killed link left
  under the name without random digits, and with a name of `NAME_MAX` bytes;
  and written in order into a FIFO, which a failed link leaves in place: for
  a reader that opens it and reads to the first end of file, with inotify
  counting one close after writing, and for one that holds the read end of a
  FIFO that may be written but not read (which only binds a user other than
  root). A directory and a path under a regular file are refused, naming the
  call. A link with an undefined symbol fails, naming it, and leaves the
  output alone; one whose `ABS32` falls below 0 removes it, and so does one
  whose `PC32` leaves int32's range, which only the checked path sees; one
  that fails while writing beside the output leaves the output, its other
  link, and nothing beside them;
- limits: a weak thread-local symbol defined nowhere (offset 0 in its field
  and its GOT slot); TLS blocks of 2 GB (linked) and past it, atoms past 2 GB
  and one whose padding wraps (refused); a bad entry point, the first of two
  symbols defined nowhere in different lanes' ranges, the smallest failing
  fixup, an atom aligned past a page and a bad init array atom, each reported
  alike with every lane count; and a lane with no scratch memory to
  spare, then 64 bytes more at a time, until the link succeeds: every failure
  must be `EXECUTABLE_ERROR_MEMORY` and leave no output;
- the patch kernel: the fast path against the checked one, on random fixups
  of every kind with wide addends, at every count from 0 to 199 from four
  starts, with the kind column ending at a `PROT_NONE` page.

`--seed N` changes the programs and the random inputs, and `--functions N`
the workers of the set named after it (2,000 by default).

## Direct executable benchmark

`./build/compiler benchmark-executable [--directory PATH] [--functions LIST]
[--lanes N] [--seed N] [--only kernels|hash|links]` (about 2 s) reports
medians of:

- the patch kernel and the variants it was chosen over (a branch on the kind,
  the checked path, 64-bit targets, and gathers with scalar stores or
  scatters) on the design's three synthetic sizes, each variant's bytes
  checked against the kernel's first;
- the chunk hash: 256 KB in L2, whole and in pieces, and 64 MB;
- links of generated programs of 1, 64, 2,000 and 180,000 workers
  (`--functions` takes a list), written into `--directory` (`TMPDIR` or
  `/tmp`) by a group of `--lanes` lanes that runs every write of a case, as a
  compiler runs its phases on one group: in place and as a new file, phase by
  phase, against the floor, one `pwrite` of the same bytes; and with one lane,
  the patch kernel on each program's own mix of kinds.

Run it under the 9700X's lock with `taskset -c 2` for one lane, or with
`taskset -c 0-7` for 8 (one CPU per core), since the lanes' threads keep the
affinity they start with. `docs/object-writer-and-linker.md` ("Patching and
writing" and "The first executables") has the numbers. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the others, its
measurements are informational.

## Refinement tests

`./build/compiler test-refinement [--count N] [--seed N] [path...]` (about
1.3 s without paths) compares `refine_tokens`, with each variant of its batched
interning (`RefineVariant`), with `refine_tokens_scalar` and with an oracle of
its own (splices and UCNs removed in plain passes, and a different table): every
identifier must get the same id and every token the same leading-space bit, on:

- the hash's three forms on spellings of 0 to 200 bytes, the predefined names
  and the keyword rules, and 300,000 random spellings that grow the table seven
  times;
- 22 boundary fixtures (splices between and inside tokens, with `\r\n`, blanks
  and trigraphs; comments; UCNs; identifiers of 62 to 65 bytes), each at all 64
  misalignments from a cache line;
- identifiers of every length from 1 to 130 bytes with a line splice (four
  spellings, one a trigraph's) at every position inside, or a UCN at every
  position, each followed by the same identifier written plainly: 50,570
  inputs;
- 20,000 random inputs (`--count N`) of identifiers, splices, comments,
  digraphs, pp-numbers and other tokens, one in 16 with trigraphs;
- 9,600 prefixes and suffixes of random inputs that end exactly at a readable
  page's end, before a `PROT_NONE` page, or start at a page's start: a read
  outside the source faults, and the test names the input and exits with 1;
- every `.c` and `.h` file under each path. On the 9700X, `src` and the C
  library corpus (`~/azp-bench/c-tokenizer/corpus/libc`) pass.

`--seed N` picks another random sequence. The 9700X job runs it on `src`.

## Refinement benchmark

`./build/compiler benchmark-refinement [--cplusplus] [--trigraphs] [--only NAME]
path...` lexes every `.c` and `.h` file once, checks every implementation
against `refine_tokens_scalar`, and reports the share of identifiers the table
finds and how often the vectorized path takes a scalar one. Then come 21
interleaved passes, each from a table reset to the predefined names, with the
medians per token and per identifier (time, cycles, instructions, IPC) of the
variants (`refine_tokens_16`, `_32`, `_64` and `_fused`; `refine_tokens` is the
64 one), the scalar path, `c_tokenize` and `c_tokenize` then refinement per file.
`docs/c-refinement.md` has the numbers for `sqlite3.c`, the C library corpus and
`src`. Run it pinned to one CPU; the default 9700X job runs it on `src`, and like
the others its measurements are informational.

## Preprocessor tests

`./build/compiler test-preprocessor [--clang] [--keep] [options] [file...]`:

- without files, the 43 built-in cases (`pp_test_cases`: cases of our own on the
  topics of the C standard's examples, the behaviors probed against GCC and
  clang, inputs past the vectorized kernels' limits, and directives and builtins
  that nest through the main loop's frames), each preprocessed by the vectorized
  kernels and their scalar twins, which must give the same stream token for
  token (kind, flags, value and location) and the same directive tables, with
  the expected spellings and the expected number of warnings;
- with files, each file with `-E`'s options (`-I`, `-isystem`, `-D`, `-std=`,
  `-ffreestanding`, ...), by both kernels, which must agree.

In both, the vectorized printer's text must be the scalar printer's byte for
byte, with line markers and without, and must lex back into the stream's
spellings.

`--clang` also compares every case and file with `clang -E -P` and the same
options (our side reads clang's own predefines for them through
`--predefines`), token spelling by token spelling, and prints the first
difference with the tokens around it. `sqlite3.c`, musl 1.2.5's 1,357 sources,
Lua 5.4.8 and this repository's translation units are token-identical;
`docs/c-preprocessor.md` has the configurations. The 9700X job runs the cases
and the repository's translation units with `--clang`.

## Preprocessor benchmark

`./build/compiler benchmark-preprocessor [--clang] [--only vectorized|scalar]
[options] file...` preprocesses each file from nothing per run (a new intern
table and preprocessor), with the vectorized kernels and their scalar twins, 21
passes after a warm-up, and reports the medians of time, cycles and instructions
per input token and per output token, IPC, each phase's share (read, lex,
refine, directive tables, main loop) and what a run did (directives, runs,
expansions, invocations, pastes, and the `#if` lines and their lengths).
`--clang` also times `clang -E -P` with the same options, a process per file.
Target options don't change our predefines: give `--predefines FILE` from
`clang -dM -E` with the same options (the benchmark leaves it out of clang's
command line), as the smoke commands above do for `main.c`. The default 9700X
job runs that one, pinned to one allowed CPU; like the others, its measurements
are informational.
