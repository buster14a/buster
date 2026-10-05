# Custom native runtime measurements (#2648)

`native-runtime-v1` measures a privately uploaded static Linux x86-64 program
using two warmups followed by nine fresh measured processes. This is a separate
recipe from the one-invocation `native-execute-v1` recipe. Both use the immutable
program upload and content identity described in [NATIVE_EXECUTION.md](NATIVE_EXECUTION.md).
No branch, public input, workflow dispatch, shell command or caller arguments
are required.

```
service client /run/buster-bench/control.sock upload-program ./microkernel
service client /run/buster-bench/control.sock submit-runtime my-runtime-001 MANIFEST_SHA256
```

The existing program upload operations 14–16 and their bounded bodies are
unchanged. A runtime submission is an ordinary five-field request naming
`native-runtime-v1`, with the same 64-digit manifest identity in both subject
fields. Journal generation 6 admits this new recipe. Generations 1–4 retain
their earlier replay semantics; a runtime request in generation 5 or below is
refused. The BQ-NATIVE-V1 input envelope retains its original `execute-once`
format field; the submitted recipe and installed profile bind the invocation
mode. An upload by itself executes nothing.

Install the exact compiled `BQ_RUNTIME_PROFILE` in `native_profile.h` as the
root-owned read-only file
`/opt/buster-bench/installed/recipes/native-runtime-v1.recipe`. The broker adds
only selector 4, stage 30 (`native-runtime`), and a fixed candidate-account
`buster-bench-service native-sampler SOURCE MANIFEST_SHA256` command. The
service-account outer selects the fixed `native-runtime-driver`. It never
executes the uploaded subject. All existing host lease, reservation, materialized
source, numeric credential, containment, named cleanup and result-binding gates
still apply.

The sampler verifies the immutable executable before and after the runs. It
uses the repository's throughput process collector with descriptor execution,
fixed argv/environment, CPU 2 and a ten-second timeout per invocation. Linux
close_range with CLOEXEC (kernel 5.11+) and seccomp must be available; launch
refuses rather than inheriting trusted descriptors if either operation fails. The wall
interval starts immediately before fork and ends after wait4: it includes
process creation, launch setup, affinity, executable loading and program work.
The reported nanoseconds are an integer representation of that interval, not
nanosecond kernel latency or timer resolution. Preparation, program hashing,
log setup, result formatting and descendant cleanup are outside that interval.
Warmup rows are retained and excluded from summary statistics.

The sealed `runtime-samples.txt` contains the original fixed header and eleven
rows. Each row contains phase, zero-based phase index, wall nanoseconds, user
nanoseconds, system nanoseconds, peak RSS bytes, minor faults, major faults,
voluntary switches, involuntary switches, diagnostics availability mask, exit
code plus one, signal number, timeout bit, launch errno and launch stage. Mask
15 means all four wait4 diagnostics were observed; a zero diagnostic remains
an observed zero. RSS is the OS high-water mark returned for the waited process,
not a sum of simultaneously live process-tree memory. PMU and allocation probes
are unavailable and are never mixed into timing.

The service validates the identity, CPU, exact warmup/sample count, row order,
field bounds and successful outcomes before sealing the original rows. It
recomputes the median/minimum/maximum wall time, median combined user+system CPU
time and maximum measured-row RSS using the repository's bounded median
implementation. The manifest binds the original row hash; the worker validates
that hash and summary again before result binding/export. A timeout, signal,
launch error, nonzero program exit or missing/truncated/malformed record cannot
become a successful result. Failed-stage diagnostics remain bounded in the
sealed `native-stage.log`.

Trusted records stay in sampler memory and its inherited pipe. The sampler is
non-dumpable; payload exec closes all trusted descriptors, replaces stdin with
`/dev/null`, and sends stdout/stderr to a separate untrusted file. The payload
cannot create a new process group or session; the sampler reaps group descendants
before the next sample; a descendant that outlives its leader invalidates the run. Core dumps are disabled and each payload file is capped
at 1 MiB. The candidate stage has a private 16 MiB scratch tmpfs and 8 MiB tmpfs
for each temporary directory, preserving the existing read-only subjects and
inaccessible queue/control/result roots. Output-file exhaustion invalidates the
run. Untrusted payload logs are not exported as trusted sample records.

These are diagnostic measurements of one supplied program. The oracle requires
exit zero and explicitly leaves the transcript unchecked. There is no compiler
preparation, paired compiler comparison, PMU or allocator result, confidence
interval, physical isolation certification or admission verdict. Installed
systemd behavior, candidate identity and remote-host measurements still require
an installed witness; local disposable tests do not establish those boundaries.
