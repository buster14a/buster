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

On desktop SysV x86-64, the defaults use fourteen successful permanent cases,
two rejection controls, and four generated cases, seed 1, a
10-second deadline per child, and at most 64 reduction trials for the first
runtime mismatch in each case. A reference compiler must be available; its
absence is a failure, not a skip. `--cc` accepts one executable, not a shell
command containing flags. The default `--reference-dialect gnu` uses Clang/GCC
syntax; on Windows specify the `.exe` path. The explicit `msvc` dialect is
described below.

Desktop SysV x86-64 hosts additionally run `sysv-sseup`: an independently
compiled observer checks sixteen-byte vector wrappers, nested wrappers, union
class merging, stack/register exhaustion, copied variadic lists and calls in
both directions. Every MIR leg is strict; NONE is a separately checked direct
oracle. This case checks actual payloads against the host compiler, so matching
Buster outputs cannot conceal a shared ABI-classification defect.

Desktop SysV x86-64 also runs `sysv-va-list`. Actual public 24-byte,
eight-aligned lists cross the Clang/GCC boundary in both directions, including
native-owned destinations, independent copies, named and unnamed GP/FP pool
exhaustion, and guarded member/array/dereference destinations. A native source
ends at an inaccessible page to detect oversized reads as well as writes.
Every MIR leg is strict; the registered driver suite additionally checks all
four allocators and both frontend forms on Linux/macOS/Android/iOS objects,
executing the matching desktop ABI with its configured host compiler.
Cross-generated mobile objects are not native mobile execution evidence.

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

The `x64-i128-float` case crosses the compiler boundary in both directions:
an independently compiled Clang or GCC caller supplies signed/unsigned 128-bit
integers and f32/f64/f80 values to the Buster subject, checks every conversion
against its own casts, and checks wide arguments and return values through the
native ABI. Inputs include the signed minimum, both sides of 2^64, and the
largest f80 value below 2^128. The three MIR allocators require zero fallback;
NONE remains the direct reference before its separate cutover. The registered
driver fixture covers Windows x86-64; this independent native comparison runs
where System V x87 long double is available.

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
`--timeout N`, `--reference-timeout N`, and `--minimize N` are validated bounded
integers; `--reference-timeout` defaults to the value of `--timeout` and applies
only to the independent compiler and its caller, link, and run processes. The
candidate matrix keeps `--timeout`, whose default is 10 seconds. Zero reduction
trials disables automatic reduction. `--reference-samples N` (1–64) is an
MSVC-only measurement option for one custom source; it records sequential
`/Od` and `/O2` subject compiles before the independent oracle run. The summary
prints the sample count, 50th percentile, 90th percentile, and maximum, while
`processes.tsv` and each sample's `.argv`, `.stdout`, and `.stderr` retain the
underlying observations. Samples do not add rows to the 432-configuration
matrix. `--no-verify` exists for testing older
compiler binaries that lack the verification flag, and is recorded explicitly.
`--strict-mir` requires `-fno-machine-fallback` for every MIR allocator and the
default mode, retaining NONE and its alias as direct controls. It is recorded in
the manifest and exact child arguments. The built-in `sysv-sseup` and
`sysv-va-list` cases always require this strict policy, including reductions,
without an extra option.

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
native host to test its ABI. The default reference dialect uses Clang/GCC-style
arguments. The MSVC subset below is opt-in.

## Native MSVC reference subset

From an x64 or ARM64 Visual Studio developer shell on **native Windows**, with
a Release `ide.exe` built for that target, run:

```powershell
./build.ps1 test_differential --ide build/Release/ide.exe --cc cl.exe `
  --reference-dialect msvc --source tools/fixtures/msvc_reference_subject.c `
  --host tools/fixtures/msvc_reference_caller.c --minimize 0 --reference-timeout 60 `
  --reference-samples 12 --strict-mir `
  --out build/differential-msvc
```

The runner checks the resolved compiler with the existing `/Bv /EP /TC`
identity and native-target probe. `VSCMD_ARG_TGT_ARCH` and a working Visual
Studio toolchain environment (`INCLUDE`, `LIB`, `PATH`) are required. The
preflight rejects a missing compiler, a non-MSVC executable, a wrong target,
built-in or generated suites, `--sanitize-oracle`, and any nonzero reduction
budget **before** creating the output directory. The default reduction budget
is 64, so pass `--minimize 0` explicitly. MSVC offers no ASan+UBSan equivalent
to the runner's sanitized reduction contract. Required Clang/GCC corpus and
sanitizer runs remain separate, unchanged obligations.

Supply a reviewed, standard-C11 source using the native Windows ABI. GNU
extensions, compiler builtins, signed-overflow-dependent behavior and
incompatible aliasing semantics are outside this subset. The mixed-object
fixture checks high-bit plain `char` under Buster's `-funsigned-char` and
MSVC's `/J`, plus explicit signed- and unsigned-char controls. `/Od` and `/O2`
are separate reference controls. The optional fixed `--host` translation unit
is compiled independently for each reference and once for all Buster
configurations. A successful custom reference must exit zero in both
optimization variants.
The included fixture checks scalar and aggregate values and calls in both
compiler directions, including an aggregate returned through the Windows ABI.

Compilation uses `/nologo /std:c11 /TC /J /Od` (or `/O2`), `/I`, `/c`, and
`/Fo`; linking uses the selected `cl.exe` with object files, `/Fe`, `/link`,
`/LIBPATH:` and `legacy_stdio_definitions.lib`. Arguments are passed directly,
including paths with spaces. The runner preserves the developer shell
environment and adds only its existing per-child sanitizer report policy.
The manifest records the resolved executable and its hash, MSVC version and
target, dialect, capabilities and environment policy; phase `.argv` files
record exact NUL-delimited arguments. Child stdout/stderr/status and stable
artifact hashes use the same evidence checks as the default dialect.
The GitHub Windows AArch64 CI step uses a 60-second deadline for the independent
MSVC references and their caller, link, and run processes. Buster candidate
processes keep the 10-second default. On AArch64, 12 extra `/Od` and 12 extra
`/O2` subject compiles report runner timings. A forced one-second timeout
self-control checks the timeout observation path on that runner.

## Observations and independent controls

The two reference executions compile the same subject with the host compiler
at O0 and O2. A failed oracle now reports its phase and cause, such as
`host-o0-compile-timeout` or `host-o0-compile-spawn-failure`, together with the
case evidence directory and `processes.tsv` path. The matching phase names the
exact `.argv`, `.stdout`, and `.stderr` files. Completed O0/O2 disagreement is
reported separately as `host-o0-o2-observations-disagree`. The default dialect
preserves `-fwrapv`,
`-fno-strict-aliasing`, and `-funsigned-char` for subjects and callers. The
MSVC subset uses `/J` and sources that do not require the first two flags.
The references must agree and terminate normally before they can be an
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

Sanitizer verdicts come from a separate compiler-rt report channel, never from
searching captured stdout or stderr. Before every child, the runner preserves the
selected environment and existing `ASAN_OPTIONS`, `UBSAN_OPTIONS`, `LSAN_OPTIONS`,
`MSAN_OPTIONS`, and `TSAN_OPTIONS`, then appends an authoritative quoted
`log_path` plus deterministic filename flags. A PID-qualified runtime file is
read only after that child is reaped, copied byte-for-byte to the observation's
`.sanitizer` evidence, and removed. Missing or unreadable report evidence fails
closed. Ordinary program output containing `AddressSanitizer`, `runtime error:`,
or any other sanitizer spelling remains ordinary byte-exact output and may be a
valid oracle observation.

The real recovering/fatal UBSan self-test controls execute only where the
host toolchain supplies a linkable compiler-rt runtime. The hosted Windows
AArch64 LLVM toolchain currently reports this control as unavailable; it is
never replaced by simulated sanitizer text, and the ordinary-output,
crash, timeout, launch, wait, and worker controls still execute.

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

The native driver also cross-links `many_native_arguments.c` and its independent
host companion in both directions. Weighted results observe every argument at
25, 33 and 65 fixed parameters, including floating and narrow scalar values,
stack aggregates, HFAs, hidden result pointers and a 65-value variadic tail.
A 522-parameter variadic signature crosses the short incoming-address range.
All six desktop targets compile with strict MIR in both frontend forms; the
matching native host executes standalone and mixed-compiler programs. The host
companion disables loop and SLP vectorization so this argument-transport check
does not require additional vector constant-pool relocation support.

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

In the default dialect, `--sanitize-oracle` instruments the host reference programs and fixed caller
translation units with ASan and UBSan and disables sanitizer recovery. Passing a sanitized Buster binary instruments
the **compiler itself**. This does not claim that Buster-generated machine code
has acquired sanitizer instrumentation. Sanitizer checks and O0/O2 agreement
are useful filters, not proofs that arbitrary supplied C has defined behavior.
Do not suppress a sanitizer report merely to get a green matrix.

## Reduction and evidence

For the default dialect, the iterative line reducer has a finite trial budget. It preserves the exact
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
embedded whitespace cannot corrupt reproduction arguments. Each case's
`evidence.tsv` records the size, hash and relative path of every stable source,
command and captured-stream artifact. Collection reopens that manifest and
every listed file, including zero-byte streams, before accepting the case.
Per-failure files
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

## Bounded case workers

`--jobs N` accepts 1 through 64 and defaults to one. The effective count is
capped by the logical CPU count, the case count and a positive decimal
`BUSTER_TEST_JOBS` quota when supplied. Invalid limits fail before running the
corpus. TCC-bootstrapped and explicitly `BUSTER_SINGLE_THREADED=1` drivers retain
one lane; the manifest records requested and effective limits. Clang/GCC-built
drivers use the existing persistent lane gang. Other build commands do not
start that gang.

A lane dynamically claims one complete case at a time. Each case keeps the
entire configuration cross product, independent O0/O2 references, fresh fixed
caller object and per-case first-mismatch reduction budget. Generated inputs
and seeds are prepared in corpus order before dispatch. Every lane owns its
arena, diagnostics, process log and evidence-error state. The arena's committed
scratch is released between cases. Spawn setup is serialized through closing
the child's pipe ends, preventing another concurrently launched child from
inheriting a capture writer; process execution and deadline waits overlap.

Each case exclusively claims its directory and writes `processes.tsv`,
`case.log`, `evidence.tsv` and a completion `result.txt`. After all lanes finish, the driver
checks one completion per case, full row counts for passing cases, the exact
completion record, the saved process/log sizes and hashes, and every unique
artifact named by the hashed evidence manifest. It then merges
process records and prints diagnostics in corpus order. Missing, duplicate,
truncated or unwritable evidence fails the aggregate. Failed child launch,
crash, timeout and recovering sanitizer reports retain their existing failure
semantics. No test is dropped when another case fails.

On Linux and macOS every compiler, linker and executed program leads a private
process group. `SIGINT` or `SIGTERM` handlers only publish cancellation flags.
Each ordinary wait lane remains the sole owner of its group identity: it reads
those flags, signals the group, proves every member quiescent while the exact
leader remains reserved, and then reaps. A two-second alarm publishes escalation
for groups which ignore termination; repeated termination signals do not extend
that interval. Spawn admission is a serialized transaction published before its
cancellation check, so a signal either closes a later transaction or leaves an
already admitted child attached to the ordinary wait lane for cleanup. A retained
cleanup failure stops further case/configuration admission without handing a raw
PGID to async code. Deadline cleanup uses the same group boundary, so a
compiler-owned helper cannot survive its timed-out parent. Once that group is
proven quiescent, capture drains the finite bytes already buffered and does not
wait for an unrelated process that inherited a writer.

The existing self-test runs this same worker and collection path with real
children at one and multiple lanes (respecting the quota), checks ordered binary
observations, and injects crashes, deadlines, failed launches, duplicate case
directories, missing/duplicate completion records and truncated or missing
per-child evidence.

Each admitted case launches at most one compiler, linker or program at once;
the compiler's own default remains one worker. `--jobs` is a CPU admission
limit, not a RAM estimator. Budget up to N simultaneous child peaks and N
case arenas when selecting it. The four hosted Unix native lanes explicitly
request four workers. The runner still clamps that request to the logical CPU
count, `BUSTER_TEST_JOBS`, and single-threaded policy; for example, the screened
three-CPU macOS AArch64 image recorded four requested and three effective
workers. Local and other invocations retain the one-worker default, and
`--jobs 1` is the direct CI reproduction path.

The hosted budget was selected from three position-balanced, full-corpus
samples of `--jobs 1`, `--jobs 2` and `--jobs 4` on each native Unix runner,
using one Release producer and fresh output directories. Exact canonical
case/configuration/observation mappings matched across all 36 corpora after
normalizing only output-root path fields, the exact output-root byte prefix in
text diagnostics, and elapsed process fields. See the [current #408 performance
audit](performance-audits/2026-09-15T072154Z.md) for source, workflow, runner,
timing, sampled process-tree RSS and retained evidence identities. Requested
four reduced the sum of per-platform median corpus wall times by 56.4% from
requested one and by 19.5% from requested two; this is a corpus-invocation
result, not a measured complete-job or workflow speedup.

### Registered admission controls

The native `--self-test` requests one, two and four workers, followed by the
existing failure-injection pass. Each request is clamped by the same host and
`BUSTER_TEST_JOBS` policy as the corpus; `DIFFERENTIAL_WORKER_CONTROL` records
the requested and effective counts separately. Single-threaded and TCC drivers
therefore report one effective lane, not an unexecuted parallel pass.

The controls rendezvous an initial cohort and keep its first successful case
active until all other cases finish. This forces unequal case lifetimes without
a wall-time performance assertion, detects a fixed per-lane partition that
strands work behind the long case, and still requires registry-order output.
An independent thirty-second rendezvous deadline releases a broken control as
a failure; the existing child deadlines are not enlarged. Live-case peaks must
equal the effective budget, completion ordinals must be unique and exhaustive,
and all cases and case-owned arenas must be inactive after the lane barrier.
These are self-test-only counters, not process RSS or corpus instrumentation.
Existing crash, timeout, failed-launch, sanitizer, missing/duplicate completion,
damaged-stream and evidence-write controls remain registered.

The Unix self-test also requests four children, applies the normal host/quota
clamp, and gives each admitted child a live grandchild. The children rendezvous
before cancelling the nested runner. The last grandchild ignores `SIGTERM` and
closes its inherited capture descriptors while its direct parent exits, proving
that wait-lane cleanup cannot lose a detached helper. Its lane injects `SIGTERM`
through the explicit shared-state test seam after cleanup proof but before the
exact child reap; the other live lanes must still be cancelled. The control requires
the helper's READY marker, forbids its post-release ESCAPED marker, and requires
the runner to retain the original `SIGTERM` status. This exercises the production
signal, wait and process-group path rather than only worker counters.

These controls are not a full-corpus one/two/four-worker timing cohort. Hosted
policy requests four workers only after the separate #408 full-corpus
qualification. Complete native-job, workflow latency, aggregate runner work and
concurrent peak-memory acceptance remain separate measurements.
