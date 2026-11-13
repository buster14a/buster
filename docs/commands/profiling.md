# profiling

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/profile.c` is the profiling tools: `ab` (builds of
  several trees run in turn, under the 9700X's lock), `time-runs` (whole
  processes, and builds compared over their code's placements and fresh
  copies of each executable, #509) and the reports of perf's
  and objdump's text, `hot-loops`, `label-spills`, `spill-report` and
  `branch-misses`. `profile_test.c` holds their tests (`main.c` includes both;
  `./build/compiler test-profile` runs them). `docs/profiling.md` is how the
  lexer and the encoder were measured and profiled on the 9700X, with these
  tools: read it before tuning a stage.

## Comparing generated Tier 1 code over layouts (#1450)

Compare Lua's run time across several **relative code layouts** before attributing
a change to Tier 1's frames, probes or instructions. In
[#1435](https://github.com/buster14a/buster/issues/1435), padding before all of
Lua's code preserved the hot functions' collisions modulo 4 KiB. Seven of eight
shifts suggested a regression; over 33 link orders, the mean of the paired log
ratios was +0.02%, with a 95% interval of -1.44% to +1.49%. The
[measurement record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/closures-stli-1435-2026-11-05)
contains the objects' orders, counters, profiles and analysis.

For a comparison of two compiler revisions:

1. Compile Lua's translation units to objects once per revision, with the same
   source, target and flags. Keep those objects fixed throughout the layout
   experiment. Relink each revision's complete object set in several recorded
   permutations, using the **same permutation for both revisions**, linker,
   libraries and link flags. Include the normal order and choose the additional
   permutations before looking at their timings. Save each actual object order,
   not just a random seed whose generator or inputs might later change.
2. Alternatively, insert padding before a unit in the middle of the link so
   that hot code on opposite sides moves relative to each other. Inspect the
   linked addresses to verify that the intended relative positions changed.
   `./build.sh shifts DIRECTORY SOURCE SHIFT...` does this for the compiler's
   own objects; see [the build guide](build.md#experiments).
   Shifting the compiler executable does not vary the layout of the Lua
   executable it generates. A single pad before all Lua objects, ASLR and fresh file
   copies alone are insufficient for this relative-layout check.
3. Check every generated executable's output against clang -O0's for every
   script before timing it. Pin timed runs to the same CPU on the 9700X and
   follow the shared lock rules above. Warm up every executable and rotate the
   revisions' order within each layout. Keep the per-run wall time, user cycles
   and instructions with the revision, layout, round and script identifiers.
4. Report the within-layout medians, the paired revision ratios by layout and
   their spread. Base an interval across layouts on the independent layouts,
   not on treating repeats of one executable as additional layouts. If the
   sign changes across layouts, or their uncertainty covers the claimed
   effect, collect more layouts or report the result as inconclusive. Keep a
   single-layout result labeled as such.

`time-runs` already accepts several executables for each named build:
`--build base BASE_ORDER_0 --build head HEAD_ORDER_0 --build base BASE_ORDER_1
--build head HEAD_ORDER_1 ... --list closures.lua`. Give each build's placements
in the same order; each run takes its round's row of a Williams design over the
slots, and its report retains the placement medians. Repeat the lists for the
other four scripts. Its
`over placements` range is a range of comparisons, **not a confidence interval
or a paired-layout estimator**; calculate paired ratios from the recorded
per-layout results when reporting those statistics. `--copies` measures a
separate file-placement effect and does not create a new relative code layout.

`measure-tier1-programs` currently builds one Lua executable per compiler. Its
repeated runs establish that layout's variation; a reported change from those
runs needs the relinked comparison above before it is attributed to a compiler
change. Preserve the commands, object hashes, link orders and raw results in
`buster-performance`, and link the record from the issue or PR. This method
does not by itself attribute #1450's compile-time instruction increase or the
Tier 0 stack-probe cost; those still need their own measurements.

## Profiling tools

`./build/compiler test-profile` (about 10 ms) checks `profile.c`. Each tool
runs on texts in objdump's, clang's and perf script's formats, and its report
must be exactly the expected one:

- `hot-loops`: a loop nested in another, an indirect jump, two copies of the
  function (the busiest one taken by the samples' addresses: their offsets in
  the file through its LOAD segments), samples of another function and of
  another file, of a name that only starts with the function's, past the last
  instruction and before the first symbol, and an lld executable, whose
  offsets aren't its addresses; samples without file offsets, a disassembly
  without objdump -p's headers, a missing function and disassembly out of
  order (errors);
- `label-spills` and `spill-report`: spills, reloads and folded reloads
  labeled with the tag in order in the function and in no other (not source
  paths or other comments that hold the words), and nothing else changed;
  their samples by stack slot and source location, the labeled copy found by
  address, without the samples of a static function of the same name in
  another unit (after it too), of the function's static local (`f.counter`),
  of another tag, of a shared library at the same offsets or of another
  tree's file of the same name, and in an lld executable; a label at the
  function's address, from the symbol table (`objdump -t`); labels out of order or of another tag, missing from the disassembly
  or in it twice, samples without file offsets, and an invalid tag (errors);
  and clang's assembly of a program whose static function `function` (also
  an assembler keyword) prints `__func__`, labeled, assembled and run, which
  must print the same;
  and one whose first instruction is a spill, labeled, linked and
  disassembled, whose report must find its labels (clang and objdump);
- `branch-misses`: records mispredicted, predicted and neither, fields that
  aren't records, the load bias and the disassembly's instructions, and
  10,000 branches, which grow its table;
- `start-order`: functions of main.c's units by their `FILE` symbols (one
  unit in two parts), a global past its unit's locals, a parallel split's
  alias, a benchmark's and the C library's functions, a name a unit's static
  shares (left out), two sample files that repeat functions, and symbols
  without functions (an error);
- `ab`: the order of a Williams design (#1311) for 1 to 17 trees, each
  tree at every place and after every other equally often over the period,
  and runs that succeed, fail and can't start, with
  two argument lists, and through a lock (`env`) pinned to an allowed CPU.
- `time-runs`: shell commands that log their order, which must be the
  warm-ups' then each run's row of the design, statuses kept as the first warm-up's
  (0 and 3), one that changes after it (a failure per run), and an executable
  that can't start; copies (#509) of scripts given as two builds of two
  placements, which log their tags and paths: the warm-up in the slots'
  order (placement by placement and build by build, then copy by copy), the
  runs in the design's first two rows, the copies' names, and
  nothing left after the run, nor after a copy that fails; and the builds'
  comparison on values worked out by hand, with placements and copies,
  without either, and with more placements than the reference's.

The commands are `ab [--rounds N] [--cpu N] [--lock PATH] TREE... --
ARGUMENT... [-- ARGUMENT...]...`, `time-runs [--runs N] [--warmups N]
[--after-scalar MS] [--copies N] [--directory PATH] [--executable PATH |
--build NAME PATH]... --list ARGUMENT... [--list ARGUMENT...]...` (whole
processes of this executable, or of each build's placements, from fresh
copies with `--copies`, in a Williams design's order, with their wall time, rusage, peak
resident set, page faults and user cycles and instructions, and the builds
compared over their placements and copies; under `ab`), `hot-loops FUNCTION DISASSEMBLY SAMPLES
[--loops N]`, `label-spills FUNCTION TAG INPUT.s OUTPUT.s`, `spill-report
FUNCTION LABELED.s DISASSEMBLY SAMPLES [--slots N]`, `branch-misses BRSTACK [--bias
ADDRESS] [--disassembly FILE] [--branches N]` and `start-order SYMBOLS
SAMPLES...` (the functions that `perf script -F ip,brstack` samples ran, by
their copies in `readelf -sW` of the executable, for
`cmake/BusterStartOrder.txt`: the build guide's "The start's code order");
`docs/profiling.md` has the
perf and objdump commands that make their inputs. Run `ab` itself outside the
9700X's lock, with `--lock ~/buster-fast-bench/exclusive.sh`, which takes it
for each run. The 9700X job's test step runs the tests.
