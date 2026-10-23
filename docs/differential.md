# External differential testing

`test-differential` runs external Csmith and YARPGen C generators, compiles each
input with Buster Tier 0, Buster Tier 1, Clang `-O0` and Clang `-O2`, then compares
their complete standard output. `test-differential-harness` tests the harness
using an original C fixture that deliberately simulates tools and programs.

This implements the harness portion of [#142](https://github.com/buster14a/buster/issues/142).
It does **not** complete that issue. At the implementation base, Buster's public
source driver preprocesses and parses; it does not emit a runnable C program.
The checker, source-to-machine integration and Tier 1 consumer remain separate
work. Neither fixture results, generator-only checks nor Clang's acceptance
establish a Buster campaign. No compiler bug reduction or external generated
corpus is added by this change.

## Commands and prerequisites

The command handlers receive arguments after the command name. `main.c` includes
`differential.c` followed by `differential_test.c` and dispatches
`run_differential_command` and `run_differential_tests`. They do not depend on
an IR or on the unfinished source driver. The code uses Linux process APIs and
the existing libc; it adds no build dependency.

Install Csmith and YARPGen as external tools. Csmith additionally needs its
matching runtime include directory when compiling its output. Clang needs a
working native C toolchain and runtime. A real Buster campaign also requires:

- An executable Buster driver whose `--version` succeeds and identifies Buster.
- An explicit Tier 0 argument, or `--tier0-default` when the driver's default is
  documented to select Tier 0. Tier 1 always needs an explicit argument.
- Source compilation and linking with `-std=c11`, `-o PATH`, `-I DIR` for Csmith,
  and both YARPGen translation units. All outputs must use the same host ABI.
- The actual generator and compiler revisions, a controlled host, and preserved
  external runtime/header installations for reproducibility.

No tier flag is guessed for the unfinished driver. Once its owner supplies the
real options, a campaign looks like this (each environment variable is set to
the actual executable, option, directory or revision):

```sh
./build/compiler test-differential \
  --buster "$BUSTER" --tier0-arg "$BUSTER_TIER0_FLAG" --tier1-arg "$BUSTER_TIER1_FLAG" \
  --csmith "$CSMITH" --csmith-include "$CSMITH_INCLUDE" --yarpgen "$YARPGEN" \
  --clang clang --generator both --seed 138 --count 10 \
  --revision "$BUSTER_REVISION" --generator-revision "$GENERATOR_REVISIONS" \
  --output /tmp/buster-differential
```

`--count` is the number of programs **per generator**, with consecutive seeds
starting at `--seed`. The defaults are both generators, both tiers, seed 1 and
one program per generator. `--tiers 0` explicitly selects an early Tier 0-only
campaign; its records contain zero Tier 1 passes. Seed 0 is rejected because
YARPGen treats it as a request for a random seed. Seed-range overflow, unknown
options, missing values and excessive argument counts are rejected.

`--buster-arg ARG`, `--clang-arg ARG`, `--tier0-arg ARG`, `--tier1-arg ARG`,
`--csmith-arg ARG` and `--yarpgen-arg ARG` are repeatable. Each occurrence adds
**one** argument. Options and their values are separate CLI entries; for example
`--yarpgen-arg --emit-pragmas=none`. There is no shell splitting, expansion or
command-string option. Tool paths and artifact directories may contain spaces,
quotes, dollar signs and shell metacharacters. Clang's optimization level and
the harness's output/source operands follow caller-supplied common arguments.

A configured Buster executable cannot be the same file as Clang; the version
probe also rejects an ordinary copied Clang executable. The preflight requires
each selected compiler mode to produce and run a new executable from an original
C source with equal, nonempty output. A diagnostics-only driver cannot pass it.
Explicitly configured wrappers remain the operator's responsibility: record
their source/revision and verify that they invoke the intended compiler/tier.

To exercise installed generators while the source compiler is unavailable:

```sh
./build/compiler test-differential --generate-only \
  --csmith "$CSMITH" --yarpgen "$YARPGEN" --generator both \
  --seed 138 --count 1 --generator-revision "$GENERATOR_REVISIONS" \
  --output /tmp/buster-generator-controls
```

Successful generation still exits **2**, with `status=unavailable`,
`generated=2`, `passed=0` and zero campaign passes. This is useful for verifying
the installed protocol without obscuring the missing compiler evidence.

The ordinary exit codes are 0 for a completed passing compiler campaign, 1 for
a failed campaign or artifact I/O, and 2 for invalid options or unavailable
tools/tier configuration/preflight. `--help` exits 0. A generator error, compiler
error, signal, timeout, missing executable, missing source, output overflow,
empty program output or output disagreement fails the case. These cases are
retained and counted, never silently skipped. All selected compiler modes run
after successful generation so a failed mode can be compared with its peers.
A Clang failure is an oracle failure requiring investigation, not proof that
Buster is wrong.

## Generator protocol and provenance

The interfaces were checked against primary upstream sources; no upstream
implementation was copied. The pinned external control builds used here are
Csmith `0cdc710315cfee9035e22ef4363ca479270d1934` (version 2.4.0) and YARPGen
`1adb290f453505838f3aa33dc571f292b0c1810f` (version 2.0).

| Tool | Version | Generation | Compilation inputs |
| --- | --- | --- | --- |
| Csmith | `--version` | `--seed N`, source on standard output | Saved `program.c`, matching runtime `-I DIR` |
| YARPGen | `--version` | `--std=c --out-dir=. --check-algo=hash --seed=N` | `driver.c`, `func.c`, with generated `init.h` |

The [Csmith README](https://github.com/csmith-project/csmith/blob/0cdc710315cfee9035e22ef4363ca479270d1934/README.md)
describes the runtime include dependency and output comparison. Its
[command interface](https://github.com/csmith-project/csmith/blob/0cdc710315cfee9035e22ef4363ca479270d1934/src/RandomProgramGenerator.cpp)
defines the version and seed options. YARPGen's
[option definitions](https://github.com/intel/yarpgen/blob/1adb290f453505838f3aa33dc571f292b0c1810f/src/options.cpp)
define C mode, output directory and seed, with `=` for long option values;
its [README](https://github.com/intel/yarpgen/blob/1adb290f453505838f3aa33dc571f292b0c1810f/README.md)
describes the multifile program and checksum oracle.

Every invocation creates a new `campaign-XXXXXX` directory under `--output`.
The directory is never reused or cleared. Two campaigns with the same seed do
not overwrite one another. Each case is named by generator and seed and retains:

- Original generated files and generator output/error, including a failed or
  partially generated case. Csmith's captured source is copied to `program.c`.
- `input.json`: generator, seed, fixture status, exact file sizes and FNV-1a-64
  content fingerprints. Fingerprints detect accidental changes; they are not
  cryptographic authentication.
- `replay.args`: a versioned, bounded, NUL-separated copy of the configuration,
  including every custom argument. It is data, not a shell script.
- One JSON command record per version probe, generation, compilation and
  execution: exact argument array and working directory, limits, outcome,
  exit code/signal/error, elapsed milliseconds, observed capture sizes,
  truncation flags and process cleanup status. Corresponding `.stdout` and
  `.stderr` files contain the bounded bytes. Executables remain alongside them.
- `result.json` and `failure.signature`, suitable for a replay or reduction.

The campaign root contains `request.json` even when tools are unavailable,
with the requested paths, arguments, caller directory, seed range and limits;
the version captures; and `provenance.json` with resolved
tool paths, binary fingerprints, caller-supplied revisions, machine identity and
the Csmith include path, `cpuinfo.txt`, preflight evidence, and `summary.json`.
The summary records both aggregate and per-generator counts and the selected tiers.
Unspecified revisions remain visibly unspecified. Keep the exact external
runtime/header installation with the results; its contents are not vendored or
copied into the repository by the harness. Environment-dependent tool behavior
must be controlled by the campaign operator; the tool records do not claim to
capture an entire machine image or a complete environment.

Sources are passed directly to all compilers in C11 mode. Standard output is
compared byte-for-byte at its complete length; prefix equality after truncation
cannot pass. Standard error is preserved separately for diagnosis. Generator
claims about defined behavior are not independently proved by this harness.
Infinite loops are possible; a timeout is retained as a failure that needs
triage, not silently attributed to a compiler bug.

## Resource and process lifetime

`--timeout-ms` applies separately to generation, version probes, compilation and
execution (default 30,000 ms; maximum one hour). `--output-limit` bounds each
captured stream (default 1 MiB), and `--source-limit` bounds generated input
(default 16 MiB total). Both accept at most 64 MiB. Csmith's source stream uses
the source limit. YARPGen files must be regular nonempty files with a combined
size within the source limit. Symlinks are rejected. Child `RLIMIT_FSIZE` also
bounds directly written files, and core dumps are disabled. A compilation may
write an executable up to 64 MiB. This is resource control for cooperative
testing, not a security sandbox for hostile tools.

The runner polls independent nonblocking stdout/stderr pipes, with bounded read
bursts so a producer cannot starve deadline checks. It writes at most the limit
to each capture, explicitly records overflow, and kills the command. Execution
uses argument arrays and no shell. Standard input is `/dev/null`; inherited
descriptors above the standard streams are closed on exec.

Each invocation has a private supervisor set as a Linux child subreaper. It
kills the command's process group, then kills and reaps adopted descendants,
including children that detached with `setsid`. Cleanup also runs when the
direct command exits successfully. An interrupted supervisor cleans the tree;
the parent-death signal requests cleanup when the harness disappears. Cleanup
has a five-second bound, and incomplete cleanup is a system error. This does
not alter the calling compiler's subreaper setting or reap unrelated children.

The runner is scalar control-plane test tooling: a case launches only a handful
of processes, and its useful work is performed in those processes. It adds no
scalar compiler-stage boundary or SIMD performance claim. Campaign throughput
and compiler quality still require controlled 9700X measurements. Run each
independent build/test/campaign under the shared `exclusive.sh` lock, with CI
priority. Use small campaign batches to release the lock between runs; pin
actual timing comparisons to CPU 2. A local generator control is not 9700X
acceptance.

## Replay and reduction

```sh
./build/compiler test-differential --replay "$ORIGINAL_CASE" --output /tmp/buster-replays
```

Replay copies retained sources into a new isolated directory and uses the saved
compiler arguments. It probes the current tool versions and fingerprints again.
Restore the recorded binaries, revisions, headers and environment before using
the result as a reproduction. `--source-directory DIR` substitutes candidate
source files without changing the saved original. Replay never invokes a
generator, and it keeps the same exit meanings as a campaign.

`tests/differential/interesting.c` is an original C adapter for C-Reduce or cvise:

```sh
clang -O2 -Wall -Werror tests/differential/interesting.c -o /tmp/buster-interesting
export BUSTER_DIFFERENTIAL_COMPILER="$ABSOLUTE_BUILD_COMPILER"
export BUSTER_DIFFERENTIAL_CASE="$ABSOLUTE_ORIGINAL_CASE"
export BUSTER_DIFFERENTIAL_OUTPUT=/tmp/buster-reduction-evidence
mkdir -p "$BUSTER_DIFFERENTIAL_OUTPUT"
```

Copy the original sources into a separate reduction workspace, change to that
workspace, and use one of these commands:

```sh
cvise /tmp/buster-interesting program.c
cvise /tmp/buster-interesting driver.c func.c init.h
```

Use `creduce` in place of `cvise` if that is the installed reducer. The adapter
runs `test-differential --interesting ORIGINAL --source-directory .` with
literal argv entries. The original configuration is read from its absolute
path; candidate source bytes are read from the reducer's current directory.
Each trial has its own artifact directory, so concurrent reducer trials cannot
overwrite one another. Monitor disk usage; the harness deliberately retains
trial evidence.

Interestingness uses the reducer convention: **0 means the same recorded
failure class persists**, and 1 means it does not. It also requires Clang O0/O2
to compile/run successfully and agree, then compiles/runs the candidate with
Clang's undefined-behavior sanitizer and recovery disabled. That sanitizer is a
filter, not a proof that the reduced source has no undefined behavior. The
failure signature distinguishes tool/mode/stage and failure class; inspect the
recorded numeric exit/signal and diagnostics before accepting a reduction.
Generator failures cannot be reduced by replaying C input.

Before committing a regression, inspect its C semantics, reduce it to the
essential behavior, record the expected output, and establish its rights and
required notices. Retain the original seed/options/tool revisions and the
failing and corrected Buster revisions. Follow the source compiler's actual
regression command when that consumer exists. No synthetic fixture is a
replacement for a source-level compiler regression.

## Licensing and validation boundary

The harness, its fixture tool, reducer adapter and this document are original
material under `Apache-2.0 WITH LLVM-exception`. The fixture's small C snippets
are handwritten for protocol tests and contain no generator output.

[Csmith's license](https://github.com/csmith-project/csmith/blob/0cdc710315cfee9035e22ef4363ca479270d1934/COPYING)
is BSD-2-Clause. [YARPGen's license](https://github.com/intel/yarpgen/blob/1adb290f453505838f3aa33dc571f292b0c1810f/LICENSE.txt)
is Apache-2.0 with a separately retained Boost notice. They are external tools;
neither their implementation nor runtime headers are included here. A generator's
license alone does not establish rights in every generated or reduced file.
Preserve emitted notices and review each proposed retained regression under
`CONTRIBUTING.md` before importing it.

`test-differential-harness` builds `tests/differential/tool.c` with external
Clang and exercises success, mismatch, compiler/generator/program nonzero exit,
signal, timeout, missing compiler output, empty output, capture truncation,
detached descendants, interruption, executable lookup failure, literal argv,
fresh artifact directories, multifile generation, replay/candidate selection,
option limits, missing tools, missing tier selection and compiler substitution.
Its artifacts always state `fixture=true` and zero compiler campaign passes.
Run it from the repository root, or supply `--fixture-source PATH`; `--clang`
selects the fixture-building compiler. Its temporary evidence is retained and
the command prints the directory.
Set `TMPDIR` to on-disk workspace storage for local diagnostics; the fixture
directory honors it and otherwise uses the system temporary directory.

The pinned real generators were also invoked through the new harness with
seed 138, producing the expected source files and recording versions/options.
That control produced two generated programs and zero Buster campaign passes.
Issue #142 remains open until both generators have recorded, passing campaigns
at both real Buster tiers on the 9700X, and any discovered bugs have reduced
source regressions.
