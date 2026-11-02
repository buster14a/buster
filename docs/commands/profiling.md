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
- `ab`: the rotating order, and runs that succeed, fail and can't start, with
  two argument lists, and through a lock (`env`) pinned to an allowed CPU.
- `time-runs`: shell commands that log their order, which must be the
  warm-ups' then each run's rotated one, statuses kept as the first warm-up's
  (0 and 3), one that changes after it (a failure per run), and an executable
  that can't start; copies (#509) of scripts given as two builds of two
  placements, which log their tags and paths: the slots' order, placement by
  placement and build by build, then copy by copy, the copies' names, and
  nothing left after the run, nor after a copy that fails; and the builds'
  comparison on values worked out by hand, with placements and copies,
  without either, and with more placements than the reference's.

The commands are `ab [--rounds N] [--cpu N] [--lock PATH] TREE... --
ARGUMENT... [-- ARGUMENT...]...`, `time-runs [--runs N] [--warmups N]
[--after-scalar MS] [--copies N] [--directory PATH] [--executable PATH |
--build NAME PATH]... --list ARGUMENT... [--list ARGUMENT...]...` (whole
processes of this executable, or of each build's placements, from fresh
copies with `--copies`, in rotated order, with their wall time, rusage, peak
resident set, page faults and user cycles and instructions, and the builds
compared over their placements and copies; under `ab`), `hot-loops FUNCTION DISASSEMBLY SAMPLES
[--loops N]`, `label-spills FUNCTION TAG INPUT.s OUTPUT.s`, `spill-report
FUNCTION LABELED.s DISASSEMBLY SAMPLES [--slots N]` and `branch-misses BRSTACK [--bias
ADDRESS] [--disassembly FILE] [--branches N]`; `docs/profiling.md` has the
perf and objdump commands that make their inputs. Run `ab` itself outside the
9700X's lock, with `--lock ~/buster-fast-bench/exclusive.sh`, which takes it
for each run. The 9700X job's test step runs the tests.
