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
  `x86_encode_address32`, the same for 32-bit addresses, with templates of its
  own); `docs/x86-64-encoder.md` explains its design and measurements.
  `x86_64_encoder_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-x86-encoder` runs them) and
  `x86_64_encoder_benchmark.c` provides `benchmark-x86-encoder`.
- `docs/object-writer-and-linker.md` is the design of the object writer and the
  linker, with the 9700X measurements of mold, wild, lld and the kernel's file
  costs behind it, and the order to build it in. The lane base layer, the
  encoder's fixups, the representation and the direct path's first
  executables below are implemented; the object writer and the rest of the
  linker aren't yet.
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
- `src/buster/compiler/executable.c` writes the direct path's first
  executables: a link in that representation laid out, patched and written as
  a static executable without libc (step 4 of the design's plan), with a build
  ID, by every lane of a group. `executable_test.c` holds its tests (`main.c`
  includes it; `./build/compiler test-executable` runs them) and
  `executable_benchmark.c` provides `benchmark-executable`.
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
./build/compiler test-link
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

`./build/compiler test-x86-encoder` (about 0.7 s) checks that every form fits a
template, both encoders on 612 known encodings from GNU as (each also with the
fixup flag, whose end they must report), a digest of the scalar encoder's output
for 100,000 random instructions, every batch width and variant of `x86_encode`
against the scalar encoder (bytes, offsets and fixup ends, in each of the four
loops: with neither, either or both of offsets and fixup ends) on every count
from 0 to 199 and on 300,000 random instructions of every form (legacy with x87
and MMX, VEX and EVEX, with opmasks, broadcasts, rounding, registers 16-31,
gathers and scatters, FS and GS segment overrides, and one in eight marked with
the fixup flag), the same between `PROT_NONE` guard pages, and that invalid
operands are rejected (immediates and rel8s outside their fields' ranges by
`x86_validate` alone: `x86_encode` writes their low bytes). Then it runs those
checks again with 32-bit addresses, but the known fixups: 33 known encodings, a
digest of their own, `x86_encode_64_address32` against the scalar encoder on the
random streams and at page edges, and invalid operands, among them MPX memory
operands and memory operands in the 80 FS and GS forms without room for the
`67`. `--address32` runs only that pass, and `--count N` and `--seed N` change
the random streams.

The digests were recorded when GNU as encoded the same streams to the same
bytes. After changing the form table, the scalar encoder or the fixup interface,
cross-check with GNU as (binutils 2.47 for the recorded digests) for both
address sizes, then record the digests the test prints:

```sh
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 > build/x86.s
as build/x86.s -o build/x86.o
./build/compiler test-x86-encoder --compare-object build/x86.o 400000 --seed 1
./build/compiler test-x86-encoder --print-assembly 400000 --seed 1 --address32 > build/x86_32.s
as build/x86_32.s -o build/x86_32.o
./build/compiler test-x86-encoder --compare-object build/x86_32.o 400000 --seed 1 --address32
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
`x86_fixup_distance` give. With `--address32`, the printer adds `addr32` where no
general register gives the address size, and the relocations of displacements
other than RIP-relative ones are R_X86_64_32. Cross-check more than one seed
after changing forms (1.7 million instructions over seeds 1, 7 and 99 were last
checked at each address size, with 199,441 relocations and with 201,280).

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
  only when the test may `chown`, as root), and past a file a killed link
  left under the old fixed name; and written in order into a FIFO, which a
  failed link leaves in place: for a reader that opens it and reads to the
  first end of file, with inotify counting one close after writing, and for
  one that holds the read end of a FIFO that may be written but not read
  (which only binds a user other than root). A directory and a path under a
  regular file are refused, naming the call. A link with an undefined symbol
  fails, naming it, and leaves the output alone; one whose `ABS32` falls
  below 0 removes it, and so does one whose `PC32` leaves int32's range,
  which only the checked path sees; one that fails while writing beside the
  output leaves the output, its other link, and nothing beside them;
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
