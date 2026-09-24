# Dedicated-host experiments

[Throughput harness](README.md) · Tracking: #46, #422 and #437.

The optional Linux qualification path records observations and serializes
**cooperating** runs. It does not certify physical isolation, idle SMT siblings,
a stable clock, usable PMU counters or a sufficiently low measurement noise
floor. A successful `qualify` command is not performance acceptance.

## Prepare outside the measurement session

Build the trusted Clang Release subjects, configure the frozen source and
headers, and run correctness/self-host gates while the measurement host is not
in use. Retain build commands, compiler versions, generated inputs and CMake
caches. Benchmark compilers must be uninstrumented. Do not build, test, download,
profile or run other benchmarks concurrently with the measurements.

The build driver currently recompiles the native harness on every
`bench_throughput` invocation. That happens **before** the native runner's lock.
Prepare the tool once while the host is idle, then invoke that immutable tool
directly during the exclusive session:

```sh
./build.sh bench_throughput self-test
./build.sh bench_throughput help
# The second command constructs build/throughput-tools/throughput.
```

This reuses the existing build policy and launcher. It does not move compiler
construction into shell orchestration or introduce a second measurement tool.
Keep the exact harness binary/source revision with the results too.

Choose an allowed logical CPU using the host topology, rather than assuming
CPU numbering or that `auto` chooses an isolated core. For example, inspect
`lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE` before measurement. Reserve its physical
core and SMT sibling and arrange housekeeping elsewhere. The suite only checks
that the requested CPU is in the current permitted affinity mask; each timed
child still uses the existing checked affinity setup. It neither reserves the
core nor stops other processes.

Choose a nonempty machine/environment label of at most 127 bytes, for example
`9700x-env1`. This is an operator assertion, not a hardware fingerprint. Use a
new environment version and repeat A/A qualification after changing firmware,
microcode, kernel, memory configuration or power policy. Retain the actual
configuration externally; this first implementation does not inventory firmware,
DIMMs, storage, thermal behavior or effective ancestor cgroup limits.

## Shared lease and read-only observations

All cooperating users and worktrees must use the **same stable file inode on a
local filesystem** in an existing controlled directory. Set `LOCK` to an
absolute path such as `/srv/buster-bench/host.lock`; arrange directory access
before the session. No privileged policy changes are performed. The file is
created when absent, without truncation, with mode 0600; no automatic permission
change is made to an existing file. The lock must remain a private regular
single-link file owned by the invoking user: group/other permission bits,
foreign ownership and extra hard links are refused. Final-component symlinks and
nonregular files are refused. Network filesystem lock semantics are outside this
contract.

```sh
BENCH=build/throughput-tools/throughput
"$BENCH" qualify --cpu "$CPU" --machine-id "$ENV_ID" --lock-file "$LOCK"
```

`CPU`, `ENV_ID` and `LOCK` must be set to the selected CPU, environment label and
shared absolute lock path. `--cpu auto` is accepted, but means only the first
permitted logical CPU. `--machine-id` and `--lock-file` must be supplied together
with `--cpu` on `run` or `qualify`. Unsupported hosts fail explicitly when this
opt-in is requested; ordinary cross-platform commands remain unchanged.

Qualification acquires a nonblocking `flock` lease, validates the selected CPU,
and prints one JSON object to stdout. A busy/invalid lease or unavailable CPU
returns 2. No output directory is created by `qualify`; the persistent lock file
may be created. A successful standalone observation is not itself a sealed run.

Never unlink, replace or rename the lock file while a holder or its descendants
could exist. Closing the descriptor releases the lease only after the last
inherited holder closes. The descriptor deliberately survives the existing
fork/exec compiler path, so an inherited holder keeps exclusion if its supervisor
is killed. There is no explicit unlock that would release surviving holders.
A compiler that deliberately closes inherited descriptors is outside that
protection. Locks in separate filesystem namespaces/inodes do not serialize.
Other builds or programs that ignore the lease are **not** prevented from running.

A service that already holds the same lease may transfer its inherited file
descriptor to the harness instead of asking the harness to open a competing
description:

```sh
"$BENCH" run \
  --baseline "$IDE" --candidate "$IDE" --output "$RESULTS" \
  --cpu "$CPU" --machine-id "$ENV_ID" --lock-file "$LOCK" --lease-fd "$LEASE_FD"
```

`--lease-fd` is Linux-only and is valid only with the required absolute
`--lock-file` and the other dedicated-host options. The inherited descriptor
must be at least 3 and name the exact inode currently selected by that path. It
must be an owner-matched private regular file with one link. The harness
reasserts a nonblocking exclusive `flock` on that same open file description;
it never unlocks or substitutes another description during handoff. A closed
descriptor, directory, final symlink, replaced path, different inode, separate
open description, shared permissions or extra link is rejected before host
capture or result creation.

After admission, the harness marks its descriptor close-on-exec before any
compiler child. The service's descriptor is in a different process and is not
changed. The harness copy stays open across preparation, warmups, measurements,
diagnostic replays, fixed-point checks, sealing and evidence finalization, and
is closed only on the CLI exit path. The service must retain its own copy for
the whole supervised job and must not unlink or replace the path. Ordinary
`--lock-file` acquisition remains inheritable and otherwise unchanged.

For `run`, the lease spans input preparation, warmups, measurements, diagnostic
replays, fixed-point checks and comparison. It is acquired before any result
directory is created and released on all normal CLI exits. Default runs without
these options take no lease. Offline `compare` takes no lease and should be run
away from an active measurement session.

## Frozen-source A/A, then A/B

Prepare absolute paths for `IDE`, `FROZEN_ROOT` and `FROZEN_GENERATED`, an exact
compiler revision label `SHA`, and a fresh `RESULTS` directory. Use the same
immutable compiler for both A/A subjects:

```sh
"$BENCH" run \
  --baseline "$IDE" --candidate "$IDE" \
  --baseline-id "$SHA" --candidate-id "$SHA" \
  --cpu "$CPU" --machine-id "$ENV_ID" --lock-file "$LOCK" \
  --output "$RESULTS" --profile ci --mode all --pairs 30 --warmups 2 \
  --self-host-root "$FROZEN_ROOT" \
  --self-host-generated "$FROZEN_GENERATED" \
  --require-identical-output

"$BENCH" compare --output "$RESULTS"
```

Then repeat with immutable baseline/candidate compilers against exactly the
same frozen inputs, header environment, flags and target. Change revision labels
accordingly. Never reuse an output directory or compare different work as a
compiler speedup. Cross-variant byte identity is required for implementation
refactors; intentional output changes need a declared semantic/ABI oracle while
retaining within-variant determinism. Canonical own-source `test_self_host`
remains a separate correctness gate. Frozen self-host jobs currently use FAST;
`--mode all` does not expand them to all allocators.

Add `--require-pmu` only after verifying the existing generic counter workflow
on the real host. Counters remain separate diagnostic replays; qualification
itself does not probe PMU access. Missing counters are not zero. This does not
supply model-specific Zen 5 events or validate physical Zen 4 behavior.

The current two-round guard remains 15% **and** 2 ms for wall time, 20% **and**
16 MiB for RSS. Thirty pairs do not tighten those margins. Exit 0 means no
confirmed substantial regression, not equivalence, a speedup or a qualified
noise floor. #426 owns the subsequent empirical A/A calibration and any opt-in
statistical policy. Retain inconclusive and failed experiments, not just wins.

## Same-source cross-build controls

The fixed 120-slot [`zen5_aa_noise.py`](../zen5_aa_noise.py) capture measures
runtime noise with one immutable compiler copied to two paths. It cannot reveal
build-root sensitivity. The #791 [matched-build audit](../../docs/performance-audits/2026-09-20T050606Z.md)
found an 80-byte `.text` placement shift and roughly 3% aggregate-ABI timing
change between trusted builds of identical source in different roots. Therefore
dedicated-host calibration also needs two **separate, predeclared** same-source
controls:

1. Build A and B serially from the same immutable source in the same configured
   root, freezing A outside that root before building B. This checks rebuild
   sensitivity within the intended matched path.
2. Build A and B serially from the same immutable source in distinct configured
   roots with identical root-normalized commands. This checks the separate-root
   confound. Do not substitute its median as a correction to a candidate result.

The admitted service must retain the complete raw compile-command manifests,
build logs, build roots, source/tree and toolchain hashes, full compile/link
argv, normalized-command digests, binary hashes, and `.text` offset, virtual
address, size and content digest. Inspect relevant function placement when a
small effect depends on layout. Each control uses the same two-round, four-swap-
block, 120-pair schedule as immutable-binary A/A. Record the actual binary
digest of both children in every slot; a logical label or path swap is not
proof of which binary ran. Keep every invalid, interrupted and superseded slot.
Freeze the control family, workloads, count and stopping rule before sampling.

`tools/zen5_build_control.py validate CAPTURE.json` checks one versioned
cross-build capture's internal consistency. `analyze CAPTURE.json --output
MODEL.json` reproduces descriptive build, path, order, drift and section-
placement summaries. Its `predeclared_family_sha256` records the claimed digest
of the frozen statistical family for independent service-receipt matching;
`control_kind` is `same-root-rebuild` or `cross-root`.
`builds.A` and `builds.B` each record the immutable source, build timing, root,
toolchain/environment, exact command argv and retained command/log digests,
frozen binary identity and `.text` facts. `staging_paths` contains the two
mutable execution paths. `observations` follow the A/A schedule and add
`first_binary_sha256` and `second_binary_sha256`. All other schedule, output,
metric, invalidity and no-optional-stopping fields follow the version-1 A/A
capture. The reader is offline: it does not build, run, authenticate a service
receipt, prove pre-sample publication, or issue a candidate verdict. Bind and
independently replay the raw files through the protected service before using
the observations for #426 completion.

## Evidence and limits

Opt-in run metadata contains `host_qualification` schema 1 inside the existing
sealed `metadata.json`. It records the operator label, lock path, selected CPU,
and bounded pre-run procfs/sysfs observations. Governor/driver/SMT topology
paths refer to the **selected CPU**, not necessarily CPU 0. The older standalone
convenience snapshots remain unchanged; do not treat their cpu0 policy files as
the selected-core qualification evidence.

Every observation includes its path, errno, truncation flag and value. A
successfully read empty file is an empty string; an unreadable/error-containing
file is null. Each value is limited to 4095 bytes, with explicit truncation;
`/proc/cpuinfo` can therefore contain only a prefix, not every processor. Raw
bytes are JSON-escaped, including high bytes. Counts and labels do not establish
bare metal, stable identity or idle hardware. The record explicitly leaves
physical isolation, SMT idleness and effective cgroup limits unverified.
`/proc/self/cgroup` records membership, not a parsed effective resource limit.

The snapshot is taken once, before preparation/timing. It is not an automatic
before/after drift detector. `/proc/meminfo`, boot identity, process IDs and
frequency observations can vary legitimately. Hashing this record is evidence
integrity, not a stable machine fingerprint or an authenticity signature.

Native tests cover lock contention, failure/exit cleanup, inherited holder
lifetime across exec, missing/empty/truncated/NUL-containing observations,
CPU selection, optional-platform refusal and sealed metadata corruption. These
are infrastructure tests, not compiler or physical-host performance results.
Physical 9700X A/A and native platform CI must be reported separately when run.

Further workload coverage stays under #346/#423; multicore CPU-set experiments
under #424. Do not use this single-CPU admission path to claim multicore scaling.

## Platform references

Linux documents the inherited open-file-description lock semantics in
[flock(2)](https://man7.org/linux/man-pages/man2/flock.2.html), and the selected-CPU
topology interface in
[CPU topology](https://docs.kernel.org/admin-guide/cputopology.html).
