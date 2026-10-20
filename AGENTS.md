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
retroactively certify historical commits. Add David's sign-off to a commit only
as "David's sign-off" below says.

## David's sign-off

Under "The maintainer's standing certification" in
[CONTRIBUTING.md](CONTRIBUTING.md), David's own agent sessions sign off their
commits for him with:

```text
Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>
```

His opt-in gates it: `BUSTER_DCO_SIGN_OFF` set to `davidgmbb` in the session's
environment, which only he sets. Where it isn't set, never add that trailer.
Where it is:

- `.githooks/commit-msg` ends each message that `git commit` or `git merge`
  writes with it, once, after the other trailers, so don't write it yourself.
  Before your first commit, check that `git config core.hooksPath` prints
  `.githooks`; if it doesn't, run `.githooks/enable-sign-off.sh`. Claude Code
  runs it at session start (`.claude/settings.json`), and so does Codex
  (`.codex/hooks.json`) in a trusted project, once David has trusted that hook
  in `/hooks`. If it reports leaving the hook off, write the trailer yourself,
  last. While `core.commentChar` is `auto`, the hook refuses every commit, since
  it can't know the comment character `git commit` picks: set the character
  explicitly, or make the commit with the opt-in unset and the trailer written
  by hand.
- Don't use `git commit -s`, which signs off as the committer
  (`Claude <noreply@anthropic.com>` in cloud sessions), or `--no-verify`, which
  skips the hook. The hook signs nothing while a rebase or a cherry-pick is in
  progress, rewords, amends and continuations included, and a plain
  `git revert` doesn't run it: for a revert, use `git revert --no-commit` and
  then `git commit`.
- Don't sign a commit from before the certification took effect, or a rebase,
  cherry-pick or amend of one: it still needs David's certification for its
  PR's head. Unset the opt-in to amend one outside a rebase
  (`env -u BUSTER_DCO_SIGN_OFF git commit --amend`) or to commit its changes
  again another way (`git cherry-pick --no-commit`), and to commit outside
  material whose provenance the PR can't record; then say so in the PR.

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
  The same command also runs `main_test.c`'s input-loader error and cleanup
  regressions; these require no corpus or performance measurement.
- `src/buster/compiler/c_lexer_benchmark.c` provides the `benchmark-lexer`
  command. The 9700X CI workflow runs it on the repository's C sources by
  default. `docs/c-lexer.md` records the GFNI classifier, the leading-comment
  skip, branch-free chunk state, the pipelined write-out, table-driven
  punctuator kinds, the delimiter scan, chunks read in place, the raw string
  check and their 9700X measurements.
- `src/buster/compiler/preprocessor_fuzz.c` provides bounded output comparison
  and the minimizer for the independent preprocessor test harness.
  `preprocessor_fuzz_generate.c` generates cases, `preprocessor_fuzz_clang.c`
  runs the Clang oracle, and `preprocessor_fuzz_test.c` provides
  `test-preprocessor-fuzz`; `main.c` includes all four.
  `preprocessor_fuzz_benchmark.c` provides `benchmark-preprocessor-fuzz`,
  measuring the external test harness. `docs/preprocessor-fuzz.md` records
  its contract. These names and locations are provisional pending agreement
  with the #117 session; no buster-fast preprocessor is attached yet.
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
  encoder's fixups, the representation, the object writer, the direct path's
  first executables and the import of the C runtime's files below are
  implemented; the rest of the linker isn't yet.
- `src/buster/compiler/lane.c` is the lane base layer that every phase of the
  object writer and the linker runs on (groups of threads that run the same
  code, their barrier and broadcast, `lane_range`, and arenas).
  `lane_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-lanes` runs them) and `lane_benchmark.c` provides `benchmark-lanes`.
- `src/buster/compiler/dataflow.c` provides aligned bitset liveness and
  interference with AVX-512 and scalar implementations, independent of IR.
  `dataflow_program.c` supplies the shared synthetic CFG generator;
  `dataflow_test.c` provides `test-dataflow` and `dataflow_benchmark.c`
  provides `benchmark-dataflow`.
  `docs/dataflow.md` records the contracts and 9700X measurements.
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
- `src/buster/compiler/import.c` holds the import path's readers: ELF
  relocatable objects into the representation, archives' members and symbol
  index, shared libraries' exports at their default versions (indexed by name
  hash), version definitions and references, and the commands of the linker
  scripts that `libc.so` and GCC's `_asneeded` libraries are. The object,
  archive and library readers each have a vector path and an exact one, which
  takes what the vector path leaves to it (the design's "Step 5's phases,
  data-parallel"). `runtime.c` finds the C runtime's files as GCC's driver
  does (`Scrt1.o`, `crti.o`, `crtbeginS.o`, `crtendS.o` and `crtn.o`, and the
  `libc.so.6` and `libc_nonshared.a` that `libc.so` names) and imports them
  into a link of their own, with what each name they refer to binds to (the
  first part of step 5 of the design's plan), each object's records as a
  piece, and an index and a filter of their global names and libc's.
  `import_test.c` holds their tests (`main.c` includes it; `./build/compiler
  test-import` runs them) and `import_benchmark.c` provides
  `benchmark-import`.
- `src/buster/compiler/bind.c` is binding (the second part of step 5): the C
  runtime that `runtime.c` imports joined to a program's link in the order
  GCC's driver links it, one symbol per global name, and each name of the link
  bound as GNU ld binds it: to the program, the runtime's objects, the linker,
  an export of `libc.so.6` at its version (an import), a member of
  `libc_nonshared.a`, which joins, or for a weak reference to nothing; with
  the link's imports and exports listed. It has a vector path and an exact
  one, its twin, which make the same link. `link_program.c`'s programs leave
  room for it (`link_program_generate_begin`, `_atoms` and `_end`).
  `bind_test.c` holds its tests (`main.c` includes it; `./build/compiler
  test-bind` runs them) and `bind_benchmark.c` provides `benchmark-bind`.
- `src/buster/compiler/executable.c` writes the direct path's first
  executables: a link in that representation laid out, patched and written as
  a static executable without libc (step 4 of the design's plan), with a build
  ID, by every lane of a group. `executable_test.c` holds its tests (`main.c`
  includes it; `./build/compiler test-executable` runs them) and
  `executable_benchmark.c` provides `benchmark-executable`.
- `src/buster/compiler/branch_sizing.c` is branch sizing (#144): the rel8 or
  rel32 form of each branch inside a function, chosen before the function is
  encoded, from `x86_encode`'s offsets and rounds over the branches alone
  (`size_branches`, its scalar twin `size_branches_scalar`, and
  `branch_sizing_validate`, the checks the two leave to the code generator, as
  `x86_encode` leaves `x86_validate`'s). `branch_sizing_generate.c` generates
  functions for its tests and benchmark: shaped like clang's -O0 and -O2 code,
  and chains of branches that take a round each. `branch_sizing_test.c` holds
  its tests (`main.c` includes it after `x86_64_encoder_test.c`, whose random
  instructions and printer it takes; `./build/compiler test-branch-sizing`
  runs them) and `branch_sizing_benchmark.c` provides
  `benchmark-branch-sizing`. `docs/branch-sizing.md` explains its design and
  measurements.
- `src/buster/compiler/call_graph.c` is the call graph check, the coding
  style's rule against recursion checked mechanically: `check-call-graph`
  reads the LLVM IR of the translation units `build.sh` compiles, builds their
  call graph and reports every cycle (Tarjan's algorithm, iterative) that isn't
  in its allowlist, `call_graph_allowed`. `./build.sh check-call-graph` writes
  that IR to a temporary directory and runs it. `call_graph_test.c` holds its
  tests (`main.c` includes both; `./build/compiler test-call-graph` runs them),
  and `call_graph_capture/` the C fixture that checks the IR capture itself.
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
./build/compiler test-preprocessor-fuzz --std c17
./build/compiler test-preprocessor-fuzz --std c23
./build/compiler benchmark-preprocessor-fuzz
./build/compiler test-x86-encoder
./build/compiler benchmark-x86-encoder
./build/compiler test-branch-sizing --cross-check
./build/compiler benchmark-branch-sizing
./build/compiler test-lanes
./build/compiler benchmark-lanes
./build/compiler test-dataflow
./build/compiler benchmark-dataflow
./build/compiler test-link
./build/compiler test-object-writer --cross-check
./build/compiler benchmark-object-writer
./build/compiler test-import --cross-check
./build/compiler benchmark-import
./build/compiler test-bind --cross-check
./build/compiler benchmark-bind
./build/compiler test-executable
./build/compiler benchmark-executable
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

Both modes also check the entry point's loader: empty and missing files,
a borrowed source and a sparse file past the lexer's length limit, and repeated
successful loads whose descriptor and four mappings must be released. A child
limits its address space to fail each token-array mapping in turn, including
`ends`, and the source mapping; each failure must retain its cause, return zero tokens and
leave no mappings or descriptor behind. This checks the existing stub only,
without replacing the driver being developed under #116, #117 and #145.

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

`./build/compiler test-x86-encoder` (about 1.8 s) checks that every form fits a
template (and that building each, in every mode, at a `PROT_NONE` page writes
nothing past it), both encoders on 761 known encodings from GNU as (each also
with the fixup flag, whose end they must report), a digest of the scalar
encoder's output for 100,000 random instructions, every batch width and variant
of `x86_encode` against the scalar encoder (bytes, offsets and fixup ends, in
each of the four loops: with neither, either or both of offsets and fixup ends)
on every count from 0 to 199 and on 300,000 random instructions of every form
(legacy with x87 and MMX, VEX and EVEX, with opmasks, broadcasts, rounding,
registers 16-31, gathers and scatters, FS and GS segment overrides, and one in
eight marked with the fixup flag), the same between `PROT_NONE` guard pages, and
that `x86_validate` finds every invalid operand and the scalar encoder rejects
it (memory operands in the 232 FS and GS forms without room for the override
among them). `x86_encode` checks nothing: it takes what `x86_validate` accepts,
as the code generator makes it (an immediate or rel8 outside its field's range,
for one, gets its low bytes). Then it runs those checks again with 32-bit
addresses, but the known fixups: 34 known encodings, a digest of their own,
`x86_encode_16_address32` and `x86_encode_64_address32` against the scalar
encoder on the random streams and at page edges, and invalid operands, among
them MPX memory operands and memory operands in the 312 FS and GS forms and the
116 forms without room for the `67`. And again for APX, but the known fixups:
153 known encodings, 31 known invalid instructions (as GNU as rejects them), a
digest of their own, `x86_encode_16_apx` and `x86_encode_64_apx` against the
scalar encoder on random streams in which half the forms have REX2 and a quarter
are EVEX ones, and at page edges, and invalid operands, among them a high byte
register with REX2, registers 16 to 31 in forms without REX2 or EVEX and XMM16
to XMM31 in SSE's forms. And in 32-bit and 16-bit modes: 159 and 150 known
encodings, 13 and 16 known fixups, a digest of each, the modes' loops at 16 and
64 instructions per iteration (`x86_encode_16_mode32` and the rest) against the
scalar encoders on random streams with both address sizes and segment overrides
in the decorations, and at page edges, instructions longer than 15 bytes (16-bit
mode has them), which `x86_validate_16` must reject, and invalid operands, among
them registers that would need REX, 16-bit addresses that aren't ModRM's, forms
of 64-bit mode alone, instructions past 15 bytes and segment overrides where an
instruction takes none. `--address32`, `--apx`, `--mode32` and `--mode16` run
only their pass, and `--count N` and `--seed N` change the random streams.

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
--vector] [--mode32 | --mode16] [--offsets] [--fixups] [--after-scalar MS]
[--only NAME]` checks every implementation against the scalar loop, then
reports 21 interleaved-pass medians per encoded instruction (time, cycles,
instructions, IPC, and the throttle: PMCx1A0's undocumented umask 0x08, raw
event `0x1000008a0`, which #199 reads as the dispatch slots Zen 5's power cap
takes). The stream is
a compiler-like mix of 16,384 legacy instructions by default (comparable with
older measurements), a mix like vectorized code (AVX-512, AVX2, BMI) with
`--vector`, or random instructions of every form with `--uniform`; `--section N`
encodes it N instructions per call, `--offsets` stores every instruction's
offset, and `--fixups` marks the calls and RIP-relative operands and reports
their ends. The variants are the batch widths (`x86_encode_4` to
`x86_encode_64`, instructions per loop iteration), the template gather, output
store and fixup store alternatives that lost, `x86_encode_64_offsets_then_pick`,
the other way to have fixup ends (every offset, then the caller picks the marked
ones), and `x86_encode_64_apx` and `x86_encode_16_apx`, APX's loops (the streams
have no registers 16 to 31, so their bytes are the same). `--after-scalar MS`
runs the scalar loop (256 instructions per call) for MS ms before each timed
pass of the other implementations: the state a compiler runs its vector phases
in. Lighter code lets the 9700X's clock boost over 30 to 300 ms, and at that
clock a heavy AVX-512 loop runs under the core's power cap for 5 to 20 ms
([#199's report](https://github.com/buster14a/buster-fast/issues/199#issuecomment-6297314071)
has the controls), longer than a pass. Use 100 to 300: from a settled clock
30 ms barely boosts it, and 100 ms reaches the state whatever the passes before
did. Compare
time as well as cycles, since the clock differs by 2% to 5% between the states.
Measure the back-to-back state with `--only NAME`, which runs one loop alone:
in full runs without the option, the loops after the scalar loop's own passes
(54 ms of the compiler mix) run partly throttled, the more so the nearer they
follow it. The entry points use 16 per iteration: after scalar code it is 0.3%
to 7% faster than 64 on every stream, while back to back 64 is up to 6% faster
on some (`docs/x86-64-encoder.md`'s "Zen 5's costs" and "Both states" have the
numbers). `--mode32` and `--mode16`
time the mode's loops at 64 and 16 per iteration (`x86_encode_64_mode32` and
`x86_encode_16_mode32`, or the 16-bit ones) and its scalar loop instead, on a
mix like 32-bit compiler output or like real-mode code (or with `--uniform`, the
mode's forms). Decide them, and any other performance question, with
measurements on the 9700X (not Zen 4, and not llvm-mca, whose predictions are
well off), using uops.info's Zen 5 port data to explain them;
`docs/x86-64-encoder.md` has the current numbers. Clang's code for the loop
changes a lot with small source changes, so compare every width after changing
the encoder, and profile which loop the samples land in. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the lexer's, its
measurements are informational.

## Branch sizing tests

`./build/compiler test-branch-sizing [--seed N] [--count N] [--cross-check]
[--keep]` (about 1.4 s on the 9700X, and 2.5 s with `--cross-check`) checks
`branch_sizing.c`. Every function it sizes must be one `branch_sizing_validate`
accepts, as the code generator's are, and both paths must give identical
results: the records, the offsets, the length, the rounds, the long branches
and the worklist's evaluations. Each result must also be the test's reference's
(relaxation over the records, as an assembler does it, with each record's
length from `x86_encode_scalar`), pass `x86_validate`, have
`x86_encode_scalar`'s offsets and length, aim every branch at its label's
record, and be long exactly where a rel8 can't reach:

- forms: every form number as a branch's record, with and without a segment
  override or the fixup flag: `branch_sizing_validate` accepts `JMP_REL8` and
  the 16 `Jcc_REL8` (found from the form table's opcodes), which grow by 3 and
  4 to the rel32 forms after them, and refuses every other;
- errors, `branch_sizing_validate`'s: a branch's index equal to the one
  before, below it, at the count and past it, a target past the count, a form
  that isn't a branch's, a branch's form with FS, GS or the fixup flag, at
  every place among 40 branches (the first error is the one reported), more
  branches than records, more records than `x86_encode` takes (which both
  paths refuse too), and a record `x86_validate` rejects, after a branch's
  error;
- boundaries: a probe branch, forward and backward, jmp and jcc, at every
  displacement from 100 to 135 and from −140 to −105 with every branch short,
  over 0 to 3 branches that grow or don't: it must end long exactly when its
  displacement with theirs is out of rel8's range;
- random functions (`--count N`, 1,500 by default) of up to 100,000 records,
  a branch every 1 to 12, at uniform and compiler-like distances, with
  `branch_sizing_generate.c`'s filler or random instructions of every form;
  also with the worklist from rounds 1, 2 and 3 and with no round limit,
  which must give the same records and offsets;
- chains (`branch_sizing_chain`) of 1 to 1,000 links, forward and backward,
  which take a round per link: up to `BUSTER_BRANCH_SIZING_ROUNDS`, then the
  worklist;
- page edges: the records, the branch columns and the offsets each ending at a
  `PROT_NONE` page, for every count up to 70 records at every branch density
  and every branch count up to 40: a read past an end faults, and the command
  then names the case and exits with status 1;
- memory: a function sized with no scratch memory to spare, then 64 bytes
  more at a time until it succeeds: every failure must be
  `BRANCH_SIZING_ERROR_MEMORY` and leave the records and offsets as they were.

`--cross-check`, which needs GNU as, prints 300 random functions, the
boundaries' probes and the chains as GNU as source, each in a section of its
own with a label at every target, and GNU as's relaxation of them (every
branch short at first, then growing the ones out of range) must give the same
bytes as `x86_encode_scalar` of the sized records; an object that can't be read,
or a function left uncompared, fails the check. The files go in a directory
under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a failure. `--seed N`
changes the generated functions. The default 9700X job runs it with
`--cross-check` after the encoder's benchmark.

## Branch sizing benchmark

`./build/compiler benchmark-branch-sizing [--seed N] [--rounds N] [--only ROW]
[--measure NAME [--samples N]]` (about 3 s) sizes pools of about 262,000
records of generated functions per row: functions of 40, 250, 2,000 and 10,000
records shaped like clang's -O0 code (tier 0's) and -O2 code
(`docs/branch-sizing.md` has the statistics they follow), a mix of the function
sizes of sqlite3 at -O0, and chains of 1,000 links forward and backward. After
checking that every way of sizing them gives the same records and offsets, it
reports medians of 21 samples, in cycles per branch and per record with IPC, of
`x86_encode` of each function (the caller's encode after sizing, the
reference), `size_branches` and `size_branches_scalar`, and each path's phases
as the difference between runs that stop after a phase and before it: the
vector path, the scalar path, and the vector path with each alternative to its
gathers measured against it (`compress` and `window`). A sample sizes the pool
in groups of at least 2,048 records, each group's records read into cache and
its branches' records put back outside the time (as generated, or as sized for
the reference encode: every whole run must come to the sized functions'
lengths), around rdpmc reads of this thread's cycles and instructions; the
pools are larger than the branch predictor learns (a pool of 33,000 records
sized again and again let it learn the scalar path's branches). `--rounds N`
sizes with another round limit, `--only ROW` runs one row, and `--measure NAME`
one measurement (an entry point's name, or a path's run through a phase, as
`vector-setup`) for `--samples N` samples, for perf. Run it under the 9700X's
lock with `taskset -c 2`. `docs/branch-sizing.md` has the numbers. The default
9700X job runs it after the tests, pinned to one allowed CPU; like the others,
its measurements are informational.

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
- the name hash: `link_hash`'s values for known names, which pin it (it is
  part of the snapshot's format), and `link_hash_names` against `link_hash`
  on random names of 0 to 200 bytes, every count to 40, every length in every
  lane of a batch, and names ending at a `PROT_NONE` page;
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
  class but the C runtime's (`EH_FRAME`, `INIT` and `FINI`) and every kind
  its options allow, and a copy of the defaults' program with the
  hexadecimal digits' fixup one byte off must print something else.

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

## Import tests

`./build/compiler test-import [--seed N] [--cross-check]` (about 0.13 s on
the 9700X, and 0.15 s with `--cross-check`) checks `import.c` and
`runtime.c`:

- round trips: each unit's object of `test-link`'s seven programs (with 500
  workers in the one that takes `--functions`), with and without function
  sections, and the object writer's unit of 33,000 functions in sections of
  their own (past `SHN_LORESERVE` sections), imported into a link of its own.
  Each of the unit's atoms must be in the atom its section became, byte for
  byte where the writer put it, each symbol at the same place with the same
  name, flags and size, and each fixup at its place, of its kind, pointing at
  the same place with the same addend, and no other fixups;
- an object made by hand with a section of every class, the sections the
  import drops (`.sframe` with its relocations, a note, `.note.GNU-stack` and
  `.comment`), every kind of symbol and every relocation type the
  representation has, out of order, with `R_X86_64_NONE` and addends past
  int32: each section, symbol and fixup imported exactly, the atoms' bytes in
  place in the file; the same with `e_shnum` and `e_shstrndx` in section 0,
  with `.eh_frame` as `SHT_PROGBITS` and with tables' `sh_entsize` 0; and one
  without a symbol table, as `crtn.o` is. Then each check a well-formed object
  never trips, one change at a time, with the error, the index of the
  section, symbol or relocation, the type and the byte offset it reports;
- archives made by hand (a 32-bit index, a 64-bit one and none, long names,
  an odd member's padding, a member of no bytes, a member without a symbol,
  an index of several of the vector path's batches with a member's symbols
  across them, and no members), then malformed ones;
- a shared library made by hand: version definitions, a name at a default
  version and at a hidden one, IFUNC, `GNU_UNIQUE`, `COMMON`, TLS and
  absolute symbols, weak and protected ones, hidden and local ones and one at
  `VER_NDX_LOCAL`, which aren't exported, references, and a name exported
  twice (the index finds the first); the same without `.gnu.version`; then
  malformed ones;
- linker scripts: `libc.so`'s and GCC's `_asneeded` ones, commas, comments
  and `OUTPUT_FORMAT`, then each error at its offset;
- page edges: every prefix of each input made by hand (the object also with
  its relocations in order, which the vector path takes), and the input with
  each byte set to 0, 0xff and a random value, ending at a `PROT_NONE` page,
  through every reader: each succeeds, or fails reporting a place within its
  input. A read past the end faults; the command then names the input and
  exits with status 1;
- the paths: each object, archive and library above (the round trips, the
  inputs made by hand, every malformed one, every page-edge input and this
  system's runtime) is read by its reader's vector path, its exact path and
  the reader itself, each into a link (or a read) of its own. The reader's
  result and records must be the exact path's; the vector path may take an
  input only when the exact path reads it without an error, and must then
  make the same records, column by column. Each well-formed input must take
  the vector path (the object made by hand, whose relocations are out of
  order, the exact one), and each check prints how many inputs each path
  took. A library's index must find what a linear scan of its exports finds;
- discovery: GCC's search under a directory of the test's own, whose newer
  version's directory has no `crtbeginS.o`: the search picks the older
  version and probes the newer one's `crtbeginS.o` too, so that installing it
  leaves the probes stale (`runtime_probes_current`) and a fresh search picks
  it;
- the runtime: `runtime_import` on this system's files. Each crt object must
  import with what it is for (`_start`, `_init` and `_fini` in `.init` and
  `.fini` atoms, crti.o's fragment first, `__dso_handle`, `__TMC_END__`),
  `libc.so.6` with its soname, `write`, `strlen`, `environ` and
  `__errno_location` at `GLIBC_2.2.5`, `__libc_start_main` at `GLIBC_2.34`
  and `GLIBC_ABI_DT_RELR`, `libc_nonshared.a` with `atexit` in its index, and
  every reference to each name bound: `main` to the program,
  `__libc_start_main` and `__cxa_finalize` to libc, `__gmon_start__` and the
  `_ITM_` functions to nothing, `__TMC_END__` to `crtendS.o`, and
  `_GLOBAL_OFFSET_TABLE_`, which the crt objects that use the GOT name, to the
  linker. Each object's piece must follow the one before, with its atoms in
  its own unit and its names' bytes one run, and the global names (binding's
  index of the runtime's names and libc's) must hold each name once, found by
  its bytes and in its filter, each global symbol's its own: `main` defined by
  none of them, `__libc_start_main` libc.so.6's, `__dso_handle` crtbeginS.o's,
  `atexit` a member's, and `_GLOBAL_OFFSET_TABLE_` and the init and fini
  arrays' bounds the linker's, whether or not the runtime refers to them.

`--cross-check`, which needs `readelf`, compares `libc.so.6`'s exports (name,
version, type, binding and size) and references with `readelf -W --dyn-syms`,
and every relocation of the crt objects and `libc_nonshared.a`'s members with
`readelf -W -r`: each one must be a fixup of its kind at its offset, against
the symbol (or the section) readelf names, with its addend, and there must be
no other fixups. `--seed N` changes the programs and the random bytes. The
default 9700X job runs it with `--cross-check` after the object writer's
benchmark.

## Import benchmark

`./build/compiler benchmark-import [--units N] [--functions N] [--seed N]
[--library PATH]... [--archive PATH]...` checks that each reader's vector
path reads its inputs as the exact path does, then reports 21-run medians of
both, the paths taking turns (time, cycles, instructions, IPC):

- objects: the object writer's objects of a generated program of `--units`
  units (64) of `--functions` workers (200), each about one of synth's
  translation units; the same with function sections; one of 33,000
  functions in sections of their own; and this system's crt objects and
  `libc_nonshared.a`'s members, per symbol table entry and relocation;
- `libc.so.6` and each `--library` (per export and reference), with its
  exports' index against the open-addressing table it replaced, built and
  looked up for every name; `libc_nonshared.a` and each `--archive` (per
  index entry); `libc.so`'s script, which has no vector path; and
  `runtime_import` end to end.

Run it under the 9700X's lock with `taskset -c 2`. With `--library
/usr/lib/libstdc++.so.6 --library /usr/lib/libLLVM.so.23.1 --archive
/usr/lib/libc.a` it measures the larger files the design's "Step 5's phases,
data-parallel" has the numbers for. The default 9700X job runs it after the
import tests, pinned to one allowed CPU; like the others, its measurements are
informational.

## Bind tests

`./build/compiler test-bind [--seed N] [--cross-check] [--keep]` (about
5 ms on the 9700X, and 0.25 s with `--cross-check`) checks `bind.c` against
this system's runtime (`runtime_import`). Every check binds by both paths,
which must make the same link, column by column, and the same imports,
exports and members:

- programs: test-link's two sets that call libc, each generated with room for
  the runtime, Scrt1.o, crti.o and crtbeginS.o joined after the program's
  symbols, its atoms, then the rest (`link_program_generate_begin`, `_atoms`
  and `_end`, which runs `link_check` on the whole link). Then: one symbol per
  global name; every name's bytes in the link's own arena and every atom's in
  the program's; every fixup's target defined, imported from `libc.so.6` at
  its export's version and with its type, the linker's, or a weak reference
  to nothing; `main` the program's, and what the crt objects refer to bound
  as GNU ld binds it (`__libc_start_main` at `GLIBC_2.34`, `__cxa_finalize`
  weakly, `__gmon_start__` and the `_ITM_` functions to nothing,
  `_GLOBAL_OFFSET_TABLE_` to the linker, `__TMC_END__` to `crtendS.o`,
  `__dso_handle` in `crtbeginS.o`, hidden); the crt objects' atoms before the
  program's and after it in GCC's order, each with the runtime's bytes and
  fixups; `.init`'s and `.fini`'s fragments `crti.o`'s first and `crtn.o`'s
  last, and `crtendS.o`'s `.eh_frame` last;
- names: every name of the programs and every export of `libc.so.6`, looked
  up in the runtime's index, must find what a scan of the runtime's names
  finds;
- rules: links made by hand, each exercising one rule, with what binding
  reports: a strong definition of `_start` (a duplicate); one of `data_start`,
  which Scrt1.o defines weakly (the program's wins), and a weak one (Scrt1.o's
  wins, first in link order); weak definitions of `_init`, with their atoms
  before and after `bind_begin` (crti.o's strong one takes the symbol, with
  its atom in `.init` and its visibility); a reference to
  `__dso_handle` (defined by crtbeginS.o's atom, hidden); strong and weak
  references to a name nothing defines (an error, and nothing); definitions
  of `strlen`, which libc.so.6 defines, and `_dl_argv`, which it refers to
  (exports), a hidden `qsort` and a name of the program's own (not); two
  symbols named `write`; references added after `bind_begin` of a name the
  runtime has and of `memcpy`; strong and weak references to `__cxa_finalize`,
  which crtbeginS.o refers to weakly; strong and weak references to
  `pthread_atfork`, which a member defines (it joins, and doesn't), and a
  hidden one (it joins); hidden and protected references to `puts`, which only
  libc.so.6 defines (an error), and a weak hidden one (nothing, not an
  import); references to the init and fini arrays' bounds (the linker's); a
  local `_init`; a symbol of the program's that it never defines (left to
  `link_check`); and a link without room. Then the rules this system's runtime
  can't reach through the entry points, on binding's own steps: a strong
  reference joining a weak one that binding left to nothing (an error, but
  not for a name left to `crtendS.o`), a name left to `crtendS.o` with source
  `RUNTIME_ARCHIVE`, and an import a member came to define dropped from the
  imports;
- an atexit program: `main` registers a handler with `atexit` and returns 3,
  the handler exits with 0. Bound, it must pass `link_check`, and `atexit.oS`
  must join, defining `atexit`, its `__cxa_atexit` imported at `GLIBC_2.2.5`
  and its `__dso_handle` bound to crtbeginS.o's;
- the linkers' cases, programs whose binding GNU ld and lld decide for
  themselves: a weak `_init` that exits with 7 if it runs, against crti.o's;
  `main` returning a weak `data_start` of 7, after Scrt1.o's of 0; a call
  through the GOT to a hidden `puts` (refused); `main` returning whether a
  weak hidden `puts` isn't 0; and references to the arrays' bounds. Each must
  bind, or fail to, as the linkers link it;
- with `--cross-check`, which needs `ld.lld`, `ld.bfd`, `cc` and `readelf`:
  each bound program written as objects, one per unit, linked by ld.lld and
  ld.bfd as a PIE with only `libc.so.6` and the loader besides, in GCC's
  order, without errors or warnings, must run and print what its model
  predicts (the atexit program must exit with 0); and the program's own
  objects, linked by `cc` with lld, must have in readelf's `.dynsym` the
  imports at their versions and with their types, the weak references with
  default visibility and the exports binding found, and lld's `--why-extract`
  the members that joined. Each linkers' case's own objects, linked by `cc`
  with GNU ld and with lld, must link, or fail to, as binding did, and run
  with the case's status (0), and so must its bound link.

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. The default 9700X job runs it with
`--cross-check` after the import benchmark.

## Bind benchmark

`./build/compiler benchmark-bind [--functions LIST] [--seed N]` binds
generated programs that call libc, of 1, 64, 2,000 and 102,400 workers by
default (`--functions` takes a list), 200 workers per unit as synth's
translation units have, by each path, and reports 21-run medians (time,
cycles, instructions, IPC) of the match (the program's global symbols looked
up in the runtime's names, per global symbol), `bind_begin` and `bind_end`,
each run on the same link put back as it was. The paths must make the same
links. Run it under the 9700X's lock with `taskset -c 2`; the design's "Step
5's phases, data-parallel" has the numbers. The default 9700X job runs it
after the bind tests, pinned to one allowed CPU; like the others, its
measurements are informational.

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
  fixup, an atom aligned past a page, a bad init array atom and an atom of
  each of the C runtime's classes, which step 4's executables have no section
  for, each reported alike with every lane count; and a lane with no scratch
  memory to spare, then 64 bytes more at a time, until the link succeeds:
  every failure must be `EXECUTABLE_ERROR_MEMORY` and leave no output;
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

## Dataflow tests

`./build/compiler test-dataflow [--seed N] [--count N]` checks the IR-independent
liveness and interference primitives in `dataflow.c`:

- generated CFGs: chains, diamonds, self-loops, nested loops, irreducible
  two-entry loops, several exits, random local components, and connected
  dispatch loops. Every shape runs with 0, 1, 2, 3, 8, 17, 65, 257 and
  2,049 blocks at each of 0, 1, 63, 64, 65, 511, 512, 513, 1,024 and 1,025
  values, then `--count` more seeds and sizes per shape (24 by default). The
  same seed must reproduce the columns and input rows. The postorder must
  visit every block once, and every row must be 64-byte aligned and padded
  to 512 bits;
- the AVX-512 and scalar fixpoints: identical `in` and `out` rows and iteration
  counts, both liveness equations satisfied, and all padding bits zero.
  Solving again over poisoned output must reproduce the result and iteration
  count, including zero padding after the poisoned rerun. Small graphs also
  use an independent per-value reachability check: a value is live when a
  path reaches its use before its definition. Graphs of 257 and 2,049 blocks
  independently check selected boundary and seed-derived values at every
  block with a reverse-reachability oracle;
- handwritten graphs: disconnected cycles, a self-loop without a use, a value
  both used and defined in one block, every combination of the transfer
  function's three input bits, the exact postorder of a chain, and repeated
  successor/self/exit edges compared with a deduplicated graph;
- interference: empty rows and every bit position in rows of 512, 1,024 and
  1,536 bits, with disjoint and intersecting inputs, checked by both paths;
- page edges: each matrix ends at a `PROT_NONE` page for every boundary value
  count above, with zero-size rows pointing at the inaccessible page itself.
  Both solvers and the interference check must stay within the rows, and the
  poisoned bytes before each matrix must remain unchanged;
- failure handling: insufficient arena capacity leaves the arena and result
  unchanged, successful allocations zero the rows, malformed successor
  offsets or targets leave the postorder and scratch arena unchanged, and
  missing scratch memory and an edge in an empty CFG are rejected.

`--seed N` changes the generated inputs (1 by default). The command reports
the check and failure counts and exits with status 1 on any failure. The
9700X job runs it after the link tests.

## Dataflow benchmark

`./build/compiler benchmark-dataflow [--seed N] [--after-scalar MS]
[--only scalar|avx512]` compares the scalar and AVX-512 fixpoints on four
generated functions: a small diamond CFG with
16 blocks and 127 values, nested loops with 256 blocks and 1,025 values, and
random local components and a connected dispatch loop, each with 4,096 blocks
and 4,097 values. The local-components case is a streaming control, not a model
of a large real function. `--seed N` selects the inputs (1 by default); generator
version 2 mixes the seed before generating topology and rows.

Each workload first checks identical rows and iteration counts, then reports
21 interleaved-pass medians and minimum/maximum ranges with alternating
implementation order. Both paths use the same repetition count, calibrated
with warm batches; every retained sample lasts at least 2 ms. Each timed solve
includes clearing `in` and `out`; generation, allocation and postorder are
outside the timed region. Both paths use direct calls through matching
`noinline` benchmark wrappers. The output gives nanoseconds, cycles and instructions
per function, cycles per block per iteration, IPC, and the throttle per block
per iteration (PMCx1A0's undocumented umask 0x08, which #199 reads as the
dispatch slots Zen 5's per-core power cap takes). Hardware counter values
are zero when unavailable. A scalar/vector disagreement fails the command.
Run at least five independent processes, releasing the host lock between
them, when recording a comparison; process-median ranges expose variation
that one process's 21 passes may miss.

The AVX-512 solver is heavy enough for the power cap, so measure it in both
states, as the encoder's benchmark does (#199): `--only avx512` times it alone,
back to back, and `--after-scalar MS` runs complete scalar solves of the
workload, untimed, for MS ms (100 to 300) before every AVX-512 sample, the state
it runs in after a compiler's lighter phases. Without either, its samples follow
the scalar ones in alternate passes and run partly throttled.
`docs/dataflow.md` has the three states' numbers.

Run it under the 9700X's lock with `taskset -c 2`. `docs/dataflow.md` records
the contracts, measurements and implementation choice. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the other benchmarks,
its measurements are informational. Building use/def rows from IR columns and
running the corpora remain outside this module's current scope and wait for
#131.

## Preprocessor harness tests

`./build/compiler test-preprocessor-fuzz [--fuzz N] [--seed N] [--clang PATH]
[--std c17|c23]` checks the independent tooling for #109. It requires Clang 23
on `PATH`, or the executable selected by `--clang PATH`. The defaults are
64 generated cases, seed 1, `clang` and C17; the 9700X job runs both C17 and
C23. The checks cover:

- deterministic generation, including every one of the 36 construct families
  even when `--fuzz N` is smaller, and repeatability for seeds 1, 7 and 99;
- independent rejection of each invalid `#`/`##` form, with acceptance after
  repairing its sole intentional fault; arithmetic and bitwise predicates
  exercise both branch outcomes, and replacing an expression with zero must
  change the accepted output tokens;
- batched Clang preprocessing against one process per case: the same
  acceptance or rejection, token kinds, spellings and token counts, with
  whitespace ignored. Cases have distinct name prefixes and marker tokens
  delimit their outputs; a rejected batch is rerun one case at a time.
  Accepted cases are also batched separately so rejection fallback cannot
  conceal a batching error;
- isolation of cases, line information and predefined counter behavior.
  `__COUNTER__` cases run alone because its state belongs to the Clang process;
- output comparison, including unequal acceptance, token counts, kinds and
  spellings, whitespace-only changes, and invalid oracle results;
- a planted comparison for the minimizer: delete complete lines, then tokens,
  while preserving the difference, until only the two required identifier
  tokens remain. Saving and reading a `fuzz_failure_N.c` fixture must preserve
  its bytes without overwriting an existing failure; the self-test removes
  its temporary files.

The oracle runs `clang -E -P -undef -nostdinc -std=c17` or `-std=c23` with
`-x c`. It resets each case's source filename and line number identically in
batched and individual runs, tokenizes the output with `c_tokenize` and splits
it at exact marker identifiers. Infrastructure failures are distinct from
source rejection.

This is test tooling outside the compile path and has no new vector path.
The minimizer currently uses only the planted comparison; these checks do
not establish the correctness of a buster-fast preprocessor. The command
names and file locations remain provisional until agreed with #117.
`docs/preprocessor-fuzz.md` describes the extension points and remaining work.

## Preprocessor harness benchmark

`./build/compiler benchmark-preprocessor-fuzz [--clang PATH] [--std c17|c23]
[--seed N]` requires Clang 23 and defaults to `clang`, C17 and seed 1. It
processes 16 generated family-0 cases (object macros and their shared
alias/redefinition scaffold) together, then in separate Clang
processes, checking that every case is accepted and the output byte totals
agree. After one warm-up pass, 21 measured passes alternate which mode runs
first and report median milliseconds per batch and cases per second.

The timing includes generation, launching Clang, temporary-file I/O and
cleanup. It measures the external test harness, not buster-fast compilation
speed. Run it under the 9700X's lock with `taskset -c 2`; the default 9700X job
runs it after the tests, pinned to one allowed CPU. Measurements are
informational; execution or output-check failures fail the command.
