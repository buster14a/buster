# Hosted correctness, native vector correctness and Zen 5 benchmarks

The workflow in `.github/workflows/9700x.yml` runs correctness tests on
GitHub-hosted `ubuntu-26.04` runners. Its matrix builds the triggering revision
with `BUSTER_HOST_ARCH=x86-64` and `BUSTER_HOST_ARCH=native`. Both use the current
official stable LLVM 23.1.4 release, including its matching C++ pragma oracle,
LLVM object/DWARF tools and lld. The installer runs only on disposable hosted
runners. GCC remains the `cc` oracle for fixtures that require GNU C extensions.

The baseline build runs on x86-64 without AVX-512. Native builds select the
existing scalar twins when the compiler's host target lacks the full SIMD
feature set. Intrinsic-only shared helpers have scalar definitions for that
build. Generated-code ISA and ABI remain controlled by the existing target
options. Both hosted variants log their CPU, target features and selected path.
The baseline leg must report `scalar`; the native leg uses vector kernels when
the full predicate in `host_isa.h` holds, and scalar kernels otherwise. A hosted
CPU without AVX-512 therefore still runs the complete correctness suite.

Standard `ubuntu-26.04` runners do not promise a particular CPU model or this
feature set. Required vector coverage comes from the real Ryzen 7 9700X, after
both hosted variants pass on the same triggering revision. The hardware job
asserts `BUSTER_HOST_VECTOR=1`, builds with `BUSTER_HOST_ARCH=znver5` and runs
the complete suite before any benchmarks. Hosted and hardware jobs read one
suite inventory, `tools/ci/correctness.tests`, with the same counts, seeds and
external cross-checks. There is no vector emulation or randomized runner pool.
Baseline memory boundary tests also run with undefined-behavior checks.
The hosted matrix additionally builds the entire compiler with each of
`BUSTER_SANITIZER=address|undefined|thread|memory`, for both host architectures.
Every variant runs `tools/ci/correctness.tests`, including the same corpus,
seeds, lane counts, generated-code execution and external cross-checks. These
are full suites; the TSan and MSan rows are not reduced module lists.
The baseline job additionally runs Buster under QEMU's `qemu64` CPU to preprocess
and compile a C fixture and assemble a multi-instruction fixture, then links and
executes their results on that same emulated CPU.
That leg links generic Ubuntu startup objects, libc and compiler support routines
from checksum-pinned packages in a private directory (`BUSTER_HOST_RUNTIME`).
It uses their loader explicitly for emulation: a hosted VM's installed startup
objects and libraries can require a newer ISA independently of `-march=x86-64`.
`BUSTER_CLANG_RESOURCE_DIR` selects the matching Clang builtin headers, preserving
their paths in line markers even when Clang is staged outside `/usr/lib/clang`.

The hosted tests fetch the frozen corpus from the
[input release](https://github.com/buster14a/buster-performance/releases/tag/ci-corpora-preprocessor-391e729af658).
`tools/ci/fetch-corpus.sh` checks the archive digest, original manifest digests,
every input byte, file counts and file kinds. The archive retains source/header
notices, package provenance and component license texts. Historical paths and
the old Clang binary hash remain provenance records; the active oracle is the
installed Clang 23 toolchain. A missing or changed input fails setup.

The `benchmarks` job runs native correctness followed by benchmarks. It has
`needs: [changes, test, fuzz]` and explicitly requires successful `test` and
`fuzz` results. GitHub applies these results to their entire matrices.
A failure, cancellation or skipped suite prevents the physical job from
being queued, including scheduled and manual runs whose change selector is
skipped. The hardware job checks out the same `github.sha`, verifies the Ryzen
7 9700X identity and vector eligibility, and retains the existing host lock,
CPU placements and benchmark selection. Native tests use eight CPUs under the
lock, and benchmarks reuse that tested executable. A native test failure stops
the job before measurements.

The job stages the current official stable Clang 23.1.4 release and matching
Clang API, linker and object/DWARF tools in the runner's private cache, verifying
the archive SHA-256 and preserving its licenses. A new completeness marker
upgrades older compiler-only caches. The pragma oracle links to the official
self-contained `libclang-cpp`, avoiding the release's Ubuntu-specific static
system-library paths on the Arch host. Frozen inputs use the same verified
archive as hosted tests. No system packages are modified.

The hosted `qualification` job retains the ruleset's existing required context,
`Build and execute on Ryzen 7 9700X`. It requires successful hosted tests and
fuzzing and the hardware job, including native correctness, so a skipped hardware job after
hosted failure cannot accidentally satisfy the merge gate. Documentation-only
changes retain their explicit no-code path.

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.

## Sanitizer builds and detection policy

Diagnostic builds default to production `-O3`, with line information, frame pointers, PIE and
`-fno-sanitize-recover=all`. Ordinary and benchmark builds retain `-O3` and their
existing fixed-address link. `build.sh print-compile-flags` and
`print-sanitizer-flags` expose the actual arguments as NUL-delimited arrays.
Instrumentation is applied to every build unit and the final link; it cannot
be combined with the private baseline runtime used for QEMU smoke tests.
`BUSTER_SANITIZER_OPTIMIZATION=1|2|3` selects an explicit optimization level.
The full hosted sanitizer matrix runs at `-O3`; additional combined ASan/UBSan
rows run the entire suite at `-O1` on both host architectures. These rows and the
fuzz campaigns run on pushes to main and on the daily and dispatched runs, not on
pull requests, whose hosted jobs are the plain builds of both architectures. This retains
diagnostic-friendly coverage without substituting it for production optimization.

| `BUSTER_SANITIZER` | Checks |
| --- | --- |
| `none` (default) | Ordinary optimized build |
| `address` | ASan, UBSan, bounds, leaks, stack use after scope and return |
| `undefined` | Independent UBSan and bounds run |
| `thread` | TSan across the correctness inventory, but the compiler's own `main.c` self-check |
| `memory` | MSan with level-two origin tracking across the correctness inventory, but the compiler's own `main.c` self-check |

ASan, UBSan and leak checking share a process. TSan and MSan have separate
builds because their runtimes cannot be combined with ASan. The independent
UBSan run also avoids ASan's allocation layout and its earlier failure points.
The checks cover the host compiler implementation. Generated machine code and
external reference tools retain their own execution and validation contracts;
bootstrap instrumentation does not instrument Buster's generated instructions.
Generated entry points do not carry Clang's function-type metadata prefix.
The native-call fixtures disable only the `function` check at their call
boundaries; ASan, the other UBSan checks and MSan remain enabled there. A separate
incorrect-function-type canary proves that normal host calls retain this check.
The source call-graph capture omits bootstrap sanitizer/coverage instrumentation,
whose runtime calls and generated globals are outside the source graph; its
analyzer still runs inside the instrumented compiler.
Resource-exhaustion fixtures retain their cases and output/fallback comparisons.
ASan, MSan and TSan builds inject compiler-owned arena-reservation and lane-start
failures in place of process-wide address-space/process limits, which can prevent
the detector from allocating shadow/fake-stack memory or starting its own threads.
Ordinary builds and standalone UBSan retain the actual OS-limit tests. No detector
is disabled for these cases; the injection controls are absent from normal builds.
The injected limits are process-wide, since lanes reserve their arenas and start
on their own threads.
A few fixtures run generated programs with FS pointing at the program's own TLS
block. The detectors reach their per-thread state through FS, so the code that runs
in that window (the link tests' libc stand-ins and C-runtime startup, and Tier 0's
call of a program with thread-locals, which switches FS by system calls of its own)
takes no sanitizer instrumentation. Outside that window the fixtures stay fully
instrumented.
Under TSan alone, the parser driver's and the units driver's walks that fail each
reservation in turn are not run: TSan maps shadow for each new mapping and keeps it, so the address space
neither returns to its base nor bounds the run's own reservations. ASan, MSan,
UBSan and ordinary builds still run it. TSan also kills a child that starts a
thread after a fork from a process of several threads, so under it
`test-lanes`' child forked from a session's lane 0 (#905) runs a group of one.

The TSan and MSan rows leave out the two commands that compile and check the
compiler's own `main.c` (#129, `tools/ci/correctness.tests`). The unit's
6,323,682 preprocessed tokens make the checking comparison's two paths hold
7.5 GiB resident unsanitized (measured with an `-O3` UBSan build on the
7940HS), and TSan's shadow took the same check past 20 GiB before it was
stopped. The `-c` commands peak at 10 GiB under TSan and 6.6 GiB under MSan
(`-O3`, `--lanes 1`), too much of a shared 16 GiB hosted runner to leave the
commands `parallel` schedules beside them safe. The plain, ASan and UBSan rows,
both host architectures, and the 9700X run both commands, so the self-check
remains required wherever the runner has the memory. Every other command runs
in every row.

`tools/ci/sanitizers.sh` replaces inherited runtime options with a fixed fatal
policy and matching symbolizer. Deliberate faults prove each enabled detector,
its failure exit code and source symbolization before the full suite starts.
These proofs use a separate directory. An unconditional report audit rejects
diagnostics from the actual suite even when a negative test expected its child
to fail. The libFuzzer crash-replay proof's planted abort writes its stack traces
to a directory of its own, and each one must name the probe's source. No
suppression file is used, and the only commands a row leaves out are the
`main.c` self-check above, for its measured memory. Reports, detector proofs
and compile commands are retained as workflow artifacts.

For example, on a supported Linux host:

```sh
BUSTER_HOST_ARCH=x86-64 BUSTER_SANITIZER=address ./build.sh
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./build/compiler test-lanes
```

Use the owner's `cores` budget where installed and the shared hardware lock on
Benchpress, as described in `AGENTS.md`.

## In-process coverage-guided fuzzing

```sh
BUSTER_FUZZ_AVAILABLE=1 ./build.sh
./build/compiler --fuzz -seed=133 -runs=10000 -max_len=4096 \
    -dict=tests/fuzz/c.dict -artifact_prefix=/absolute/crashes/ /absolute/corpus/
```

Fuzz builds default to the combined `address` mode at `-O3`. Every compiler unit receives
`-fsanitize=fuzzer-no-link`; the link uses Clang's matching no-main libFuzzer
runtime. `--fuzz` hands control to `LLVMFuzzerRunDriver` before ordinary command
dispatch, preserving engine options, coverage, mutation, crash artifacts,
minimization and replay. Other compiler commands continue to work in that
executable. A missing runtime or incompatible TSan configuration is an error.

The default original callback in `src/buster/compiler/fuzz.c` accepts inputs up
to 64 KiB. It compares scalar/native lexer output and exercises preprocessing,
parsing, checker-input construction, typed expression checking and initializer
planning. Rejection of malformed C is normal. All per-input owning state is
released, and source-directed include/embed lookups and existence queries
cannot read external files. Permanent enabled/disabled controls verify that
boundary against existing absolute paths. This callback does not execute
input-derived machine code or claim to fuzz every backend phase.

To replace the callback while retaining the same instrumented compiler and
driver, provide a C file that defines
`int buster_fuzz_test_input(const uint8_t* data, size_t size)`:

```sh
BUSTER_FUZZ_AVAILABLE=1 BUSTER_FUZZ_ENTRY=tests/fuzz/entry_override.c ./build.sh
./build/compiler --fuzz -runs=10000 /absolute/elf-corpus/
```

The file is included in the driver unit after the module interfaces, so it can
call their APIs directly. The shipped override exercises the ELF object/shared
header reader. It is an actual replacement for the default frontend callback.
Do not retain borrowed input pointers after the callback returns.

Hosted CI runs bounded frontend and ELF campaigns with seed 133, original C
seeds and newly compiled original ELF fixtures. Main pushes run 10,000
inputs per campaign; scheduled/manual runs use 100,000. A third, deliberately
crashing entry proves the override, saved crash bytes and exact-input replay.
All three jobs retain their corpora, crash inputs, logs, compile commands and
matching executable, and on those runs must pass before hardware qualification can start.
