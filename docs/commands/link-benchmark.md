# link-benchmark

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/link_benchmark.c` provides `benchmark-link`, the whole
  link (step 5's last part): generated programs that call libc linked by the
  direct path (the runtime opened from its snapshot, binding and the dynamic
  PIE's write) and, from the object writer's objects, by GNU ld, lld, mold and
  wild as the design's appendix links them, with the work each does.
  `floors_benchmark.c` provides `benchmark-floors`: what the machine and the
  kernel charge a linker (the design's "The floors"). The two take over the
  appendix's harness and floor programs.
## Link benchmark

`./build/compiler benchmark-link [--functions LIST [--filler N]] [--seed N]
[--directory PATH] [--cache PATH] [--cpus LIST] [--linkers LIST] [--runs N]
[--only direct|linkers] [--kinds LIST] [--keep]` (about 122 s on the 9700X)
links generated programs that call libc, without `SIZE32` and `SIZE64`, as
dynamic PIEs, static executables, static PIEs and shared libraries (`--kinds`, a
list of `dynamic`, `static`, `static-pie` and `shared`, every kind by default),
by the direct path and by each linker, and reports medians:

- the programs: one worker and 2,000 workers (about hello world's and tcc's
  outputs), and one of synth's shape, 102,400 workers in 512 units with up to
  64 filler instructions each (44 MB); or with `--functions`, a program of
  each count of workers, of up to `--filler` (8) filler instructions each;
- the direct path, on a lane per CPU of `--cpus` (CPU 2): `runtime_open` (or
  `runtime_open_static`, `runtime_open_static_pie`), which must load the
  kind's snapshot in `--cache` (the cache directory), and `bind_begin`; then,
  after the program's atoms, `bind_end` and `dynamic_inputs` (or
  `dynamic_static_inputs`, `dynamic_static_pie_inputs`), and
  `executable_write` (a static PIE's with `ExecutableOptions.dynamic` and
  `.runtime` both set) over the file of the run before into a directory under
  `--directory` (`TMPDIR` or `/tmp`). Each part is the
  median of its runs after a warm-up, each run from the link and binding as
  they were before the part, put back outside the time (a static link's
  COMDAT signatures among them), and must bind the link the first binding
  made (its atoms, symbols and fixups) and write the warm-up's file again (its
  build ID); the link is the parts' sum. A binding that finds a piece of the
  snapshot damaged (`BIND_ERROR_SNAPSHOT`) throws the measurement away: the
  snapshot is discarded and written again, outside any measurement, and the
  program measured again from the start, once. Then its work: the bytes
  written, the fixups applied, the names looked up, over the warm-ups the
  page faults and how much the peak resident set grew, and a timed run's page
  faults in `runtime_open` and in `bind_begin` (the snapshot's mapping's,
  #490); and the floor, one
  `pwrite` of the output;
- the objects, each unit's as `-c` writes them, on the same lanes;
- each linker of `--linkers` (`ld.bfd`, `ld.lld`, `mold`, `wild` and `link-foreign`) linking
  them with the appendix's command, the clang driver's with the matched
  options (its `-static` one for a static executable, with
  `--start-group -lgcc -lgcc_eh -lc --end-group`, and its `-static-pie` one
  for a static PIE: `-static -pie --no-dynamic-linker -z text`, `rcrt1.o`,
  `crtbeginS.o` and `crtendS.o`, with the same group), with its own threads
  and on one (pinned to the first of `--cpus`):
  the wall time from `posix_spawn` to the wait's return, over the output of
  the run before, the median of 41 runs after a warm-up with a 20 ms pause
  before each, or past 4 MB of output 11 runs after 200 ms, and GNU ld's 5
  after 500 ms (`--runs N` sets every count). Then one run more without
  forking, through the command itself as a helper (`--rusage`), whose rusage
  gives its page faults and peak resident set: a program spawned from the
  benchmark would have the benchmark's peak resident set as its own when that
  was higher, which Linux carries over at `exec`. The helper's own floor, a
  program that does nothing, is printed first;
- `link-foreign` (#86, #394), this executable's linker of other compilers'
  objects, given the same command line as a process of its own, for every
  kind: by default for its threads (the lanes its objects pay for, #594),
  with `--lanes=8`, the most its default takes, and with `--lanes=1` for one,
  on every CPU as its threads run and pinned as one thread runs: a default
  link of one lane runs where the scheduler puts it, on the 9700X up to 60%
  slower than on CPU 2.

Every program written must run and print what its model predicts, but wild's
static executables and static PIEs of a program whose `.tbss` is more aligned
than its `.tdata`, whose TLS segment wild 0.10 starts at `.tdata`'s
alignment, off its own wherever the layout puts it there, so that glibc's
static startup faults: the benchmark doesn't run wild on such a program, and
says so. `--only direct` and `--only linkers` run one side, and
`--keep` leaves the directory.
Run it under the 9700X's lock without `taskset`: it pins its lanes and the
linkers' runs on one thread itself, and the linkers' other runs need every CPU.
`docs/object-writer-and-linker.md` ("The whole link") has the numbers. The
default 9700X job runs it after benchmark-snapshot, with its lane on the job's
first allowed CPU; like the others, its measurements are informational. The
linkers' side, about 121 s of its 122, times other programs on the machine,
the same for every pull request: the job runs it on the daily and dispatched
runs and on pull requests that change `link_benchmark.c` or `floors_benchmark.c`,
and only the direct path (`--only direct`, about 1 s) on other pull requests.
