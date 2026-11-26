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
- **Measure Release.** `./build.sh` builds Develop, the edit loop's
  configuration ([the build guide](build.md#configurations)); every tree that
  `ab` or `time-runs` compares, and every executable `perf` profiles, is built
  with `./build.sh --config Release` (`shifts` builds Release by default).

## Measuring a hand edit

`tools/performance/dev.sh` has three commands. Each prints its results directory
under `build/perf-runs/`. Use `--out NEW_DIR` before the command to name a run.
For editor navigation, point clangd at the compilation database published by
`./build.sh` with `clangd --compile-commands-dir=build`.

### Compile paths and generated programs

This example has an externally visible kernel for disassembly and checks its own
answer. Substitute the translation unit you are editing for a representative run.

```sh
p=tools/performance/dev.sh
$p build
cat > build/mix.c <<'C'
unsigned mix(unsigned x) { return x * 33u + 1u; }
int main(void)
{
    unsigned x = 0;
    for (unsigned i = 0; i < 1000000; i += 1) x = mix(x);
    return x != 0x6b071e40u;
}
C
$p --out build/mix-before paths --runs 9 --run build/mix.c
$p asm build/mix-before/direct.o mix
```

| Row in `paths.tsv` | What Buster compiles |
| --- | --- |
| `direct` | Single-TU `-c` through the fused C-to-ELF64 compiler. Admission requires the `buster direct` producer string in the object's `.comment`. A bailout writes `direct.skipped` and excludes that row. An explicit `--only direct` fails on bailout. |
| `tier0` | `--no-direct -c` forces the full Tier 0 pipeline on the same source. |
| `tier1` | `--tier1 -c` uses the normal Tier 1 policy. The separate diagnostic compile records `compiled` and `fallbacks` in `tier1.stages`. For an exclusively Tier 1 workload, use `paths --only tier1 file.c -- --tier1-strict`. |

`compile.txt` contains the interleaved `time-runs` samples after warmup, wall
medians and ranges, user/system CPU, wait4 peak RSS, faults and, when the kernel
allows counting, user cycles and instructions. Each path also saves its ELF
object, section sizes and one separate `--time-stages` breakdown. Single-lane
compiles are pinned. `--compiler FILE` selects an existing Release binary.

`--run` links each object with `cc -no-pie` (or `CC`), checks a successful exit
and identical stdout across paths, then compares executable runtimes in
`runtime.txt`. Use a single-TU program with a deterministic result and enough
work in `main` to exceed process startup time. Pass each workload argument with
`--arg`. For an object without `main`, omit `--run`.

```sh
$p paths --only direct --cpu 2 --runs 11 \
    --events cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u \
    --record build/mix.c -- -std=c17
```

`--events` runs a separate `perf stat` and saves its per-event counts. `--record`
saves a user-cycle profile and a text hotspot report. With `--run` both switches
also profile the generated programs. Pick events exposed by `perf list` on the
measurement CPU. `commands.sh` records argv, and `context.txt` records the
revision, worktree status and machine. `inputs.sha256` identifies the compiler and source.

### Build latency and Clang passes

```sh
$p build --config Develop --jobs 4
$p build --trace --report --config Develop --jobs 4
$p build --trace --report --config Develop --target buster_c_direct
```

Keep configuration, job count and timing options fixed between edits. The first
instrumented build starts its own tree. Repeat immediately for a no-op baseline,
then edit and repeat for incremental cost. A plain `build` measures the normal
tree. The underlying build publishes the selected executable and compile
database as usual. Use Release for compiler throughput and generated-code work.

| File | What to read |
| --- | --- |
| `build.log` | End-to-end wall, user and system time inside the host grant. `--report` includes Clang's pass timing reports. |
| `phases.tsv` | Build-driver wall time in microseconds for setup, tree lock, configure check, preparation, previous stage, Ninja, receipt collection and publication when applicable. |
| `edges.tsv` | Start, finish and duration in milliseconds for each Ninja command in this invocation. The first output identifies a multi-output step. Splitting, compiling a part, joining, linking and baking appear when those steps run. Parallel intervals overlap. |
| `traces/` | With `--trace`, verbose Clang time-trace JSON for rebuilt C units and native IR parts. Open one in Perfetto or `chrome://tracing`. A no-op build emits no new traces. |

If Ninja compacts its log during a build, the wrapper reports that the
per-step receipt was unavailable. The build log and phase timings still apply.

### Function disassembly and instruction costs

```sh
$p asm build/compiler c_direct_try_compile
$p asm build/mix-before/tier1.o mix --mca znver4 --mca znver5
$p asm build/mix-before/direct.o mix --uops "$HOME/.cache/buster/uops/instructions.xml"
```

Plain disassembly needs Python 3 and GNU binutils. It prints Intel syntax, raw
bytes and relocations for one sized ELF function. Use `nm -nS` to find its
emitted name. For duplicate local names, select the defining object. An inlined
function may have code only in its callers.

`--mca` uses `llvm-mc` to decode the bytes and `llvm-mca` to print each
instruction's modeled µops, latency, reciprocal throughput and resource
pressure. Install LLVM tools with the requested CPU models on PATH or set
`LLVM_MC` and `LLVM_MCA` to versioned binaries. These are LLVM scheduling
estimates. `runtime.txt` measures the executed paths and loop counts.

For uops.info, build the XED-to-XML Python extension once and download the
measurement XML. The example pins the decoder and its build helper used for
qualification. The extension build needs a C toolchain, Python development
headers and setuptools. Use the same Python interpreter to build and run it.

```sh
uops_dir=$HOME/.cache/buster/uops
mkdir -p "$uops_dir"
(
    cd "$uops_dir"
    git clone https://github.com/andreas-abel/XED-to-XML.git
    git -C XED-to-XML checkout 56fe1b1c228c2407fbde6b2de24b7b39d1e8da9a
    git clone https://github.com/intelxed/mbuild.git
    git -C mbuild checkout 1b437e409221a2b5703b4d8896baa20d43e4ba1a
    cd XED-to-XML
    python3 mfile.py --opt=2 --no-encoder pymodule
)
curl -fL https://uops.info/instructions.xml -o "$uops_dir/instructions.xml"
export PYTHONPATH="$uops_dir/XED-to-XML${PYTHONPATH:+:$PYTHONPATH}"
$p asm build/mix-before/direct.o mix --uops "$uops_dir/instructions.xml"
```

The lookup matches each instruction's bytes, XED form and XML attributes.
It prints the separately published Zen 4 and Zen 5 measurements, available
ports and throughputs, operand-pair latencies and upper bounds. Same-register
and indexed-address measurement variants retain their XML suffixes. Missing
measurements and ambiguous forms are marked unavailable. The script reads the
local XML offline. [uops.info data and attribution](https://uops.info/xml.html) ·
[XED-to-XML](https://github.com/andreas-abel/XED-to-XML)

### Running on the 9700X

Point `--9700x` at your own checkout on benchpress, including the scripts and
the source edits you intend to measure. The checkout path can be absolute or
relative to the remote home. Input paths refer to that checkout. This example uses `build/mix.c` in that checkout.

```sh
checkout=buster-fast-bench/hand/buster
$p --9700x "$checkout" build --jobs 8
$p --9700x "$checkout" --out build/mix-9700x paths --runs 9 --run build/mix.c
$p asm build/mix-9700x/direct.o mix --mca znver5
```

Each remote build or measurement takes the existing benchpress lock. The
default host target is `znver5` and the measurement CPU is 2. Set
`EXCLUSIVE_OWNER`, `EXCLUSIVE_TITLE` and `EXCLUSIVE_LINK` locally to identify
your run. Benchpress commands have a five-minute timeout (30 minutes for a
build). Set `BUSTER_PERF_TIMEOUT` to change it for a larger workload.
The wrapper uses Tailscale SSH, prints the remote results path and
copies results back to the local output directory. The downloaded objects can
be annotated offline on your workstation. [Benchpress access and host setup](benchpress.md)

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
  shares (left out), two sample files that repeat functions, the second's
  new one listed after the first's, and symbols without functions (an
  error); a file of three traces, its functions by how many of them ran
  each (one that a trace ran twice counted once), alone and after a file
  that ran one of them first;
- `start-trace`: its reader of an executable's functions on an image made
  in the test (an alias, a function outside the instructions, one without a
  size, an undefined one and an object; a file cut short, and bytes that are
  no executable); this executable's own `-dumpversion` traced twice into one
  file, without its exit helper, whose two traces must hold the same
  addresses in the same order, each once, `main`'s among them; and a path
  that isn't there (an error). A sanitizer's build leaves the live trace
  out: LeakSanitizer stops its own process with ptrace as it exits, which a
  traced process refuses;
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
ADDRESS] [--disassembly FILE] [--branches N]`, `start-trace OUTPUT
COMMAND...` (runs COMMAND, whose first word is its executable's path, with a
breakpoint at the entry of every function of the executable, each removed as
it is reached, and appends to OUTPUT a line `trace` and the address of each
function the command ran, in the order they first ran: what it runs,
exactly; its exit status is the command's) and `start-order SYMBOLS
SAMPLES...` (the functions that `start-trace`'s traces or `perf script -F
ip,brstack` samples ran, by their copies in `readelf -sW` of the executable,
grouped by the first sample file that ran each, smallest command first,
each group in address order, or for a file of several traces by how many of
them ran each, for `cmake/BusterStartOrder.txt`: the build guide's "The
start's code order");
`docs/profiling.md` has the
perf and objdump commands that make their inputs. Run `ab` itself outside the
9700X's lock, with `--lock ~/buster-fast-bench/exclusive.sh`, which takes it
for each run. The 9700X job's test step runs the tests.

## Regression verdicts

The native `build/performance/paired` reader consumes bounded `time-runs`
captures with an identical-base control. Its plan fixes whether a unit is a
matched relative layout or a Williams-ordered block. The JSON report gives
absolute timings, paired effects, uncertainty, the practical budget and the
control result. [Reader, calibration and baseline commands](../performance/readers.md#paired-performance-verdicts)
