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

The defaults use thirteen permanent cases and four generated cases, seed 1, a
10-second deadline per child, and at most 64 reduction trials for the first
runtime mismatch in each case. A reference compiler must be available; its
absence is a failure, not a skip. `--cc` accepts a Clang/GCC-style executable,
not a shell command containing flags. On Windows specify the `.exe` path.

The native variadic case also exercises ELF AArch64's independent integer
and floating-point argument files. Its ten-float call exhausts the floating
registers, and its mixed named parameters check the anonymous integer cursor.
Both directions cross compiler boundaries, with copied lists and stack
canaries. Homogeneous floating aggregates cover two- and four-element reads,
including a spill that closes the floating register file before a smaller
following argument. Machine unit tests select and verify the fixture for ELF
AArch64 as well as x86-64 and execute it when the host ABI matches.

The registered driver suite runs strict MIR platform variadic tests on native
Darwin and Windows AArch64, outside the direct-emitter comparison matrix.
Its separately compiled callers and callees cover named floating arguments,
mixed anonymous integer/floating arguments, integer register exhaustion,
Darwin's packed narrow named stack arguments, copied lists, and scalar,
homogeneous-floating and integer aggregates up to sixteen bytes. Public
`va_list` objects cross the compiler boundary in both directions: copying a
by-value list preserves the producer's cursor, while advancing through a
pointer changes it. The standalone source also serves the cross-target driver
checks; writing a Darwin or Windows object on another host is not native
execution evidence. The former direct emitter has independently reproduced
public-list and Darwin named-stack ABI defects, so agreement with it does not
serve as the oracle for these new public-ABI cases.

On ELF AArch64, twenty-two additional relations exchange actual public `va_list`
objects, rather than only calling variadic functions compiled by the other
compiler. Independent producers and consumers check the three-pointer/two-offset
layout, pointer and by-value list parameters, original-versus-copy independence,
canaries, mixed named register/stack arguments, i128 register/stack alignment,
HFA overflow and independent GP/FP exhaustion. By-value calls pass a separate
`va_copy`, end that consumed copy before any further use, and continue through
an independent original list. The consumer checks distinct caller/callee storage;
no check relies on reusing the consumed by-value list. The extended caller/reader cases cover 24/32-byte
HFAs, 17-byte and over-aligned indirect composites, 64/128-bit short vectors,
and two/four-part HVAs, including mixed vector lane types. They exhaust both
register files and check sixteen-byte overflow alignment after a double-sized
stack slot, including named vector/HVA parameters before the anonymous tail.
Direct V-register staging retains preceding X arguments in every
allocator mode. Quad-precision floats and vectors wider than sixteen bytes
remain outside these read fixtures. The existing native configuration matrix
runs them on Linux AArch64; other hosts do not count the guarded relations as
executed. QEMU cross-compiler checks are separate emulator evidence.

The native aggregate case covers Windows indirect arguments in both call
directions, including 3/5/7-byte values, 12/16/24/32-byte values, aligned
objects, hidden return pointers and function-pointer calls. Volatile writes
to callee parameters check that caller values remain unchanged. Large
anonymous aggregates are read by the independent Clang callee. Machine tests
also verify that distinct calls reuse one aligned outgoing copy area. The
fixture has a validated Linux x86-64 and AArch64 baseline. Calls to locally
defined functions with qualified aggregate parameters under strict verification
are a separate frontend issue tracked in [#361](https://github.com/buster14a/buster/issues/361).

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

Today the Cartesian product is **432 configurations per case**:

* Four explicit allocators: NONE, MIR_STACK, FAST, QUALITY, plus the
  `-fno-register-allocator` alias and the implicit default.
* Nine optimization choices: no flag, `-O`, `-O0`, `-O1`, `-O2`, `-O3`, `-Os`,
  `-Oz`, and `-Ofast`.
* Frontend SSA on/off, canonical-local promotion on/off, and target-local
  promotion on/off (eight combinations). The `_pN` row suffix uses bit 0 for
  canonical promotion, bit 1 for target promotion, and bit 2 for frontend SSA.

There are 288 explicit-allocator combinations and 144 alias/default controls.
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
at O0 and O2. All subject and host-caller compilations preserve `-fwrapv`,
`-fno-strict-aliasing`, and `-funsigned-char`. The references must agree and terminate normally before they can be an
oracle. Nonzero program exits are valid observations, not automatically errors:
the observables fixture deliberately exits 37 and writes a NUL byte to stdout.
The built-in ABI fixtures additionally require the independent reference to
exit zero, so a broken test expectation cannot bless every compiler mode.

Every Buster row compares stdout, stderr, and exit status against that oracle.
Signals/Windows exception statuses, timeouts, failed launches/waits, missing output
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
isolate the callee's local copy, whose alignment fix already landed in
[PR #282](https://github.com/buster14a/buster/pull/282). No alignment fix is
part of this harness change.

The qualified-aggregate fixture cross-links top-level const/volatile parameter
objects with an independent host caller and callees. It checks private-copy
semantics, compatible function pointers selected across a loop join, expression
function-pointer types, large by-value objects, and nested pointer qualifiers.
The minimal source regression and frontend invariant test independently retain
strict fixed-argument type matching and volatile accesses (GitHub #361).

The unsigned-switch fixture cross-links 32- and 64-bit switch functions with a
Clang-built caller. It checks high-bit case constants, default edges, and values
that share their low 32 bits but must remain distinct in a 64-bit comparison.
The machine unit tests additionally require zero fallback in all four modes.

The clear-cache fixture checks empty and short unaligned ranges, both argument
side effects, and values kept live across the operation. AArch64 machine
tests also compare the emitted sequence with the independent Clang assembly
fixture, including both loop targets and barriers. These checks do not assume
that a hardware cache failure is observable on every host.

The CPU-query fixture compares CPUID outputs with a separately compiled host
caller, including reordered tied inputs, indirect output places, and inputs
kept live after the query. The host pins a value in RBX across the call to
check callee-save preservation. Leaves zero and `0x80000000` avoid the
processor-specific APIC ID returned by leaf one. Non-x86 hosts exercise the
fixture's portable branch. Machine tests additionally cover XGETBV encoding,
reject a target without XSAVE, and execute it only when CPUID reports OSXSAVE.

The native-variadic fixture checks calls in both directions against the host
compiler. It exercises integer and float register exhaustion, named parameters
on the stack, independent copied lists with local canaries, hidden
result pointers, and one-, two-, four-, eight- and sixteen-byte aggregates.
Machine tests require zero fallback for its six callees on x86-64 Linux,
macOS, Windows and UEFI in both frontend forms and every allocator, and execute
the matching host ABI. The sixteen-byte variadic aggregates are passed to
Buster by the host compiler; Windows indirect aggregate callers retain their
existing separate selection restriction.

## IR/MIR checks

`ide cc -fverify-codegen` opts out of the certified-IR/selector fast paths.
It runs canonical validation through preparation, validates selected MIR, and
validates rescheduled QUALITY MIR when scheduling changes it. An invalid
selected/rescheduled function or placement is fatal instead of being hidden by canonical
fallback. Ordinary compilation retains its existing fast paths.

Successful native compilation prints:

```text
CODEGEN_VERIFY version=1 ir=1 mir=2 scheduled=0 allocator=fast
```

The runner requires exactly one supported marker, a positive canonical-module
count, strict bounded decimal fields, and the requested effective allocator.
Scheduled counts cannot exceed selected counts. Raw counts remain in the saved
stdout. NONE has no selected MIR. Unsupported selection can legitimately fall
back without producing MIR, so the runner does **not** falsely require every
function to have been selected. These are the existing structural validators
and placement-builder validity checks, not a new proof of register-allocation
semantics; the executable/ABI comparisons provide the independent behavioral
check.

`--sanitize-oracle` instruments the host reference programs and fixed caller
translation units with ASan and UBSan and disables sanitizer recovery. Passing a sanitized Buster binary instruments
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
real child output/status/crash/deadline capture. Wait failures, missing
artifacts, failed links, malformed telemetry and invalid ABI expectations also
have explicit negative controls. They do not need `ide` or a
host C compiler.

## CI integration

GitHub's four independent Unix `native` lanes run the self-tests and the
sanitized native reference matrix as a separate step after the execution-mode
matrix. The step is included in the native result summary. Its process logs and
source artifacts are packed into `native-ci-logs.tar.gz` inside the `native-*`
artifact, beside `result.json` and `summary.md`; generated executables and
objects are excluded. See
[native evidence packaging](ci-suite-partition.md#native-evidence-packaging).
Forgejo's dedicated Linux and macOS lanes also run the native
matrix. This wiring does not establish that an unavailable native runner ran;
report the actual submitted-revision checks separately.

The `va-list-places` case keeps all lists within one translation unit and checks
builtin aliases, member/index/dereference destinations, independent copies and
side-effect counts against Clang at O0/O2. It exercises frontend list handling
without exchanging public list objects across the AArch64 compiler boundary.
