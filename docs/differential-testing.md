# Native configuration differential testing

`build.c` owns `test_differential`; its implementation is in
`tools/differential.c`. It complements, rather than replaces, the existing
seeded Python differential corpus, `test_all`, and `test_mode_matrix`.
There is no new runtime dependency or shell-based process orchestration.

## Run

From the repository root, build `ide` normally and use a **new** output directory:

```sh
./build.sh test_differential --ide build/Release/ide --cc clang --out build/differential-release --sanitize-oracle
```

The defaults use six permanent cases and four generated cases, seed 1, a
10-second deadline per child, and at most 64 reduction trials for the first
runtime mismatch in each case. A reference compiler must be available; its
absence is a failure, not a skip. `--cc` accepts a Clang/GCC-style executable,
not a shell command containing flags. On Windows specify the `.exe` path.

```sh
./build.sh test_differential --self-test
./build.sh test_differential --list-configurations
./build.sh test_differential --source path/to/reproducer.c --host path/to/fixed-caller.c --include path/to/includes --out build/differential-reproducer --minimize 128
./build.sh test_differential --source path/to/invalid.c --reject --out build/differential-diagnostics
./build.sh test_differential --ide path/to/sanitized/ide --out build/differential-sanitized --sanitize-oracle
```

`--source` replaces the built-in corpus. Without `--host`, the source must
contain `main`; with it, Buster compiles only the subject translation unit and
the host compiler links it to the fixed caller/observer translation unit.
This also accepts saved C cases from `tools/differential_c_harness.py`.
`--host` and `--reject` cannot be combined. The original subject's directory
remains on the include path during reduction. `--generated N`, `--seed N`,
`--timeout N`, and `--minimize N` are validated bounded integers; zero reduction
trials disables automatic reduction. `--no-verify` exists for testing older
compiler binaries that lack the verification flag, and is recorded explicitly.

## Configuration authority

The driver and runner consume the same `codegen_configurations.h` lists, so
adding an accepted allocator or optimization spelling adds matrix rows rather
than silently leaving a second hard-coded list stale. The driver asserts that
the allocator registry contains every enum mode.

Today the Cartesian product is **216 configurations per case**:

* Four explicit allocators: NONE, MIR_STACK, FAST, QUALITY, plus the
  `-fno-register-allocator` alias and the implicit default.
* Nine optimization choices: no flag, `-O`, `-O0`, `-O1`, `-O2`, `-O3`, `-Os`,
  `-Oz`, and `-Ofast`.
* Canonical-local promotion on/off crossed with target-local promotion on/off.

There are 144 explicit-allocator combinations and 72 alias/default controls.
These are accepted flag spellings, not a claim that all optimization spellings
implement different passes. Optimization flags precede allocator flags because
the driver's existing last-option-wins rule lets a later `-O` restore FAST.
QUALITY exercises its scheduling policy; there is no invented scheduler flag.

This runner executes the **native target**. It does not pretend a successfully
written cross-target object was executed. Continue running `test_mode_matrix`
for the separate target/object/disassembly matrix, and run this command on each
native host to test its ABI. The host compiler interface is Clang/GCC-style;
MSVC's `cl` command-line syntax is not implemented.

## Observations and independent controls

The two reference executions compile the same subject with the host compiler
at O0 and O2. They must agree and terminate normally before they can be an
oracle. Nonzero program exits are valid observations, not automatically errors:
the observables fixture deliberately exits 37 and writes a NUL byte to stdout.
The built-in ABI fixtures additionally require the independent reference to
exit zero, so a broken test expectation cannot bless every compiler mode.

Every Buster row compares stdout, stderr, and exit status against that oracle.
Signals/Windows exception statuses, timeouts, failed launches, missing output
files, compiler/linker failures, and sanitizer reports are failures even when
all configurations fail alike. The process log retains the native status word;
a signal is not collapsed into an ordinary exit code. A recovering sanitizer
report is a failure even if its process exits zero. Existing output directories
are atomically refused, and objects/executables are removed before each compile.

Compiler diagnostics are compared across Buster configurations, not against
Clang's wording. Rejection fixtures require ordinary nonzero compiler exits,
never a crash or timeout. Successful-warning, syntax-error, and type-error
fixtures exercise both paths. The sole normalization is the validated
`CODEGEN_VERIFY` telemetry line; real warnings and errors remain byte-exact.

Mixed-compiler fixtures test host-to-Buster integer/FP arguments after register
exhaustion, narrow arguments, mixed register-class aggregates, structure
returns, and Buster-to-host calls. Separate aligned-parameter callees test
32-, 64-, and 128-byte natural alignment, including exhausted integer argument
registers. The observer is in a separate translation unit, preventing a host
compiler from folding the pointer check from the callee's type declaration.
Outgoing over-aligned Buster calls are a distinct issue; these regressions
isolate the callee's local copy (#238), not the changes in PR #237.

## IR/MIR checks

`ide cc -fverify-codegen` opts out of the certified-IR/selector fast paths.
It runs canonical validation through preparation, validates selected MIR, and
validates rescheduled QUALITY MIR when scheduling changes it. An invalid
selected function or placement is fatal instead of being hidden by canonical
fallback. Ordinary compilation retains its existing fast paths.

Successful native compilation prints:

```text
CODEGEN_VERIFY version=1 ir=1 mir=2 scheduled=0 allocator=fast
```

The runner requires exactly one supported marker, a positive canonical-module
count, and the requested effective allocator. Raw counts remain in the saved
stdout. NONE has no selected MIR. Unsupported selection can legitimately fall
back without producing MIR, so the runner does **not** falsely require every
function to have been selected. These are the existing structural validators
and placement-builder validity checks, not a new proof of register-allocation
semantics; the executable/ABI comparisons provide the independent behavioral
check.

`--sanitize-oracle` instruments the host reference programs with ASan and UBSan
and disables sanitizer recovery. Passing a sanitized Buster binary instruments
the **compiler itself**. This does not claim that Buster-generated machine code
has acquired sanitizer instrumentation. Sanitizer checks and O0/O2 agreement
are useful filters, not proofs that arbitrary supplied C has defined behavior.
Do not suppress a sanitizer report merely to get a green matrix.

## Reduction and evidence

The iterative line reducer has a finite trial budget. It preserves the exact
runtime difference mask and failing Buster configuration against agreeing
host O0/O2 references. Each reduction candidate must compile/link and terminate
normally; reference programs are ASan/UBSan-instrumented during reduction even
when the main run did not request instrumentation. Invalid candidates and
reference disagreement are rejected. The fixed ABI caller is never reduced.
The final saved source is recompiled and rechecked before `confirmed=1` is
written. The reducer is not guaranteed globally minimal or 1-minimal when its
budget expires; preprocessor-heavy and single-line cases may not shrink.
Compile failures, diagnostics, crashes and timeouts are retained but are not
automatically source-reduced by this implementation.

The output directory contains `manifest.txt` with compiler paths, binary sizes
and `buster_hash_64` fingerprints (not cryptographic hashes),
`configurations.txt`, `processes.tsv`, and a final summary. Every phase saves
byte-exact `.stdout`/`.stderr` and a NUL-delimited `.argv` file, so quoting or
embedded whitespace cannot corrupt reproduction arguments. Per-failure files
record the difference signature (runtime bits: kind=1, status=2, stdout=4,
stderr=8, sanitizer=16; 100+ denotes a compiler/infrastructure/verification
failure, and 170 denotes changed compiler diagnostics); reduction saves the original source,
`minimized.c`, trial logs, final rechecks, and `reduction.txt`. Keep the fixed
caller and original include tree with a standalone reproducer. Infrastructure
and evidence-writing errors make the command fail.

Self-tests exercise comparison masks, binary output, nonzero exits, shared
crash/rejection failures, recovering sanitizer detection, number validation,
matrix uniqueness, telemetry validation, atomic output-directory claims, and
real child output/status/crash/deadline capture. They do not need `ide` or a
host C compiler.
