# Hosted correctness, native vector correctness and Zen 5 benchmarks

The workflow in `.github/workflows/9700x.yml` runs correctness tests on
GitHub-hosted `ubuntu-26.04` runners. Its matrix builds the triggering revision
with `BUSTER_HOST_ARCH=x86-64` and `BUSTER_HOST_ARCH=native`. Both use the current
official stable LLVM 23.1.4 release, including its matching C++ pragma oracle,
LLVM object/DWARF tools and lld. The installer runs only on disposable hosted
runners. GCC remains the `cc` oracle for fixtures that require GNU C extensions.

The build pins `-fno-stack-protector` (#916). A distribution's Clang can
default to `-fstack-protector-strong` while the official release does not, so
the host's configured default would otherwise decide the compiler's code and
its measurements. The owner chose no stack protection for now, which matches
the official release's default and gives host and CI builds the same canary
count. The compiler's predefined macros still follow the host driver's
configured default (`BUSTER_HOST_SSP_DEFAULT`). The build's post-link step
checks each linked executable for a `__stack_chk_fail` reference and fails if
the policy did not take effect.

The baseline build runs on x86-64 without AVX-512. Native builds select the
existing scalar twins when the compiler's host target lacks the full SIMD
feature set. Intrinsic-only shared helpers have scalar definitions for that
build. Generated-code ISA and ABI remain controlled by the existing target
options. Both hosted variants log their CPU, target features and selected path.
The baseline leg must report `scalar`; the native leg uses vector kernels when
the full predicate in `host_isa.h` holds, and scalar kernels otherwise. A hosted
CPU without AVX-512 therefore still runs the complete correctness suite.

Each matrix row runs its inventory as 4 shards (`matrix.shard`), a runner each,
dealt by `parallel --shard K/4 --costs tools/ci/correctness.costs`: the runner
deals the commands to the shards by their costs on a hosted runner, the
costliest first, the same deal on every shard's runner, so that a row's shards
run every command once (docs/commands/parallel.md). Unsharded, a plain row's
test step took 14.6 minutes of its 18-minute job on one 4-CPU runner, and the
MSan rows took up to 98 minutes. Each shard builds the compiler itself, since a
native build must run on the CPU model it was built for, which hosted runners
don't share. A row's once-only steps (the baseline fallback checks, the QEMU
smoke test, the constrained arenas, the parser smoke test, the performance
readers and the TSan/MSan `main.c` compiles) run in its shard 1, and each
shard's sanitizer reports are an artifact of its own.

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
are full suites, a row's 4 shards together; the TSan and MSan rows are not
reduced module lists.
The baseline job additionally runs Buster under QEMU's `qemu64` CPU to preprocess
and compile a C fixture and assemble a multi-instruction fixture, then links and
executes their results on that same emulated CPU.
That leg links generic Ubuntu startup objects, libc and compiler support routines
from checksum-pinned packages in a private directory (`BUSTER_HOST_RUNTIME`).
It uses their loader explicitly for emulation: a hosted VM's installed startup
objects and libraries can require a newer ISA independently of `-march=x86-64`.
`BUSTER_CLANG_RESOURCE_DIR` selects the matching Clang builtin headers, preserving
their paths in line markers even when Clang is staged outside `/usr/lib/clang`.

The self-host qualification is a separate workflow,
`.github/workflows/selfhost.yml`, and also runs only on GitHub-hosted runners.
It builds the compiler with the compiler, runs a bounded smoke subset, and
compares deterministic probe objects and executables, once for each host path:
`native` (the vector path, skipped on a runner without it) and `x86-64` (the
scalar path, #1193). It is not part of the required correctness or benchmark
path; see [the self-host guide](self-hosting-ci.md).

The hosted tests fetch the frozen corpus from the
[input release](https://github.com/buster14a/buster-performance/releases/tag/ci-corpora-preprocessor-391e729af658).
`tools/ci/fetch-corpus.sh` checks the archive digest, original manifest digests,
every input byte, file counts and file kinds. The archive retains source/header
notices, package provenance and component license texts. Historical paths and
the old Clang binary hash remain provenance records; the active oracle is the
installed Clang 23 toolchain. A missing or changed input fails setup.

The `benchmarks` job runs native correctness followed by benchmarks. It has
`needs: [changes, evidence, test, fuzz]` and explicitly requires successful
`test` and `fuzz` results, and the `evidence` job's success and admission.
GitHub applies these results to their entire matrices. A failure, cancellation
or skipped suite prevents the physical job from being queued, including
scheduled and manual runs whose change selector is skipped. The one skipped
suite that admits it is a nightly dispatch's: its `evidence` job found the
commit's own earlier run had passed the plain rows. A nightly backlog run and
an admitted snapshot run are in the *plain* scope, as a pull request is: plain
rows only, and no fuzzing (see "Nightly main measurements"). The hardware job checks out the same `github.sha`, verifies the Ryzen
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

The job keeps its verified setup inputs in the runner's private cache
(`BUSTER_CI_CACHE`). Each one is kept under a key that identifies it, checked
on every use, published atomically, and replaced when stale or corrupt:

- **Frozen corpus:** the archive, under its SHA-256
  (`tools/ci/cached-download.sh`). Every extracted file is still verified
  against the manifests.
- **c-testsuite:** a bare mirror holding the pinned commit
  (`tools/ci/fetch-c-testsuite.sh`). The checkout fetches it through Git's pack
  protocol, which checks every object's hash.
- **Pragma oracle:** the executable (`tools/ci/pragma-oracle-cache.sh`), under
  a key made of its source's SHA-256, the toolchain's content manifest, the
  compiler's path and version, llvm-config's flags and the command. It is
  checked against its recorded SHA-256 and must answer `--version`.

The staged Clang release itself has a content manifest
(`tools/ci/tree-manifest.sh`): the SHA-256 of every file and the target of
every link, written when `setup-benchpress.sh` publishes the tree. Every use
checks the tree against it (about 0.15 s for 600 MB, in 8 processes) and
stages it afresh from the verified archive when anything differs. A tree
staged before the manifest is staged afresh once.

The oracle's cache checks the manifest again and keys on its digest. So a
compiler, header or library changed in place can't reuse an oracle another one
built: the check refuses the tree, and a fresh staging is another key.

On 2026-11-03 PR jobs, the setup before native tests took about 45 s, nearly
all of it the 40 s build. The corpus and c-testsuite fetches took 0 to 1 s, and
the oracle's 2.3 s ran beside the build (#1183). So these caches remove the
host's network dependence more than they save time.

The build's critical path is one unit, `main_unit_checker.c` (35 s). Only a
content-addressed object cache could take time off it, and only when that
unit's preprocessed input repeats. That needs a compiler launcher, which
`build.sh` deliberately clears, so it is left as an open item. Native
correctness (about 110 s) and the runner hook (1 to 92 s, including lock waits)
are the other costs. `tests/ci/caches.sh` tests each cache's hit, miss,
corruption and changed input.

The hosted `qualification` job retains the ruleset's existing required context,
`Build and execute on Ryzen 7 9700X`. It requires the `evidence` job's success,
successful hosted tests and fuzzing, and the hardware job, including native
correctness. A skipped hardware job after a hosted failure therefore cannot
satisfy the merge gate. Documentation-only changes keep their explicit no-code
path.

One skipped hardware job does pass: a push to main in the nightly mode whose
`evidence` job reused its pull request's native qualification of the identical
tree (see "Nightly main measurements"). The context's message then names the
source run. A nightly dispatch reports through its coverage record and has no
such context, and neither does a scheduled run the nightly mode retired.

Draft PRs still run hosted correctness, but do not queue the 9700X. Both
`ready_for_review` and `converted_to_draft` trigger the workflow, using the
same per-PR concurrency group: marking ready qualifies the current revision,
and returning to draft cancels the superseded ready run. A code-changing draft
has a **failing required qualification**, with an explicit withheld-hardware
message, not a green skipped native job. The selector-confirmed no-code path
is still allowed, including on drafts; it reports an exemption, not vector
coverage. Performance-document/harness exceptions remain selector-controlled.

`tools/ci/qualification.c` is the aggregate's fail-closed predicate. It checks
selector/event consistency as well as every required result; missing, invalid,
failed or cancelled results cannot pass. Its `--self-test` enumerates
50,000,000 combinations against the explicit allowed tuples. They span the
event, draft state, selection, dispatch purpose, the `evidence` job's hardware
decision, and seven job results.
The hosted selector job runs that test before any hardware admission, and the
selector's own policy mutants pin the draft guard and ready/draft triggers.

A pull request's run stops at its first failure. Its two test rows' shards fail
fast, and its tests wait for the agent-guide size and DCO hook checks, which finish
within the `changes` job's time, and don't start when either fails, so the
9700X job never runs for a pull request whose qualification can't pass. Pushes
to main, the daily run and dispatched runs keep `fail-fast: false` and test
whatever those checks report: their sanitizer rows and fuzz campaigns are main's
coverage, and one detector's failure must not hide what the others find. The
Tier 1 machine-problem workflow's two architectures follow the same rule.
GitHub has no fail-fast across jobs. A job that cancels the run through the API
was considered (#1195) and dropped: it needs a token with `actions: write`,
which fork pull requests don't get; on a pull request, once the policy checks
gate the tests, nothing would remain for it to stop; and on the other runs it
would cut the coverage above.

To check CI edits locally, run the selector's self-test and validate the shell
and workflow syntax. To exercise fallback code on a capable machine, set
`BUSTER_HOST_ARCH=x86-64` for `build.sh`; keep Zen 5 performance measurements on
Benchpress.

## CI latency baseline

`.github/workflows/ci-timings.yml` runs entirely on hosted runners, at 07:07
Europe/Madrid or on manual dispatch. It reads at most the latest 100 runs of
`9700x.yml`, fully paginates each run's latest-attempt jobs, and reports the
median and nearest-rank 95th percentile of successful hardware jobs' queue,
execution, runner-hook, setup/build, native-test, benchmark-step and
host-occupation durations. Host occupation runs from the job's first step,
after the runner hook took the lock, to the job's end. Four classes stay
separate:

- PR;
- main push;
- nightly targeted: backlog dispatches, recognized by their run name;
- full: the daily run, full dispatches and the nightly snapshot's run.

The classes say how a run was started, not what it covered: the coverage
records do that. The raw API responses,
normalized observations and summary are retained as 14-day workflow artifacts.
This adds no benchmark work and changes no hardware scheduling or merge gate.

Missing or inconsistent timestamps contribute no sample; a job or step that
failed/skipped is not a successful timing observation. No observations means
unknown, not zero. `runner-hook` includes hook overhead and possible waiting;
**it is not the exact shared-lock wait**. Exact lock acquisition/release and
therefore exact lock occupation still require host-hook instrumentation, which
this report does not pretend to infer from `started_at`.

The collector (`tools/ci/timings.sh`) uses read-only Actions access on the
hosted runner. `tools/ci/timings.c` validates bounded tab-separated observations,
rejects duplicates/invalid numbers and computes statistics. The PR path tests
both without API credentials or network calls:

```sh
bash tests/ci/timings.sh
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/qualification.c -o /tmp/qualification && /tmp/qualification --self-test
```

## Nightly main measurements

Main's first-parent states (#1207) are measured on the 9700X between 04:00 and
07:00 Europe/Madrid. A branch's internal commits are not states; a merge of
them is. Each state needs **targeted** coverage: the selector's choice against
its own first parent, with native correctness first in the same job. A state
whose first-parent change is documentation alone (push's paths-ignore) needs
none, and the ledger says so. The cycle's snapshot also needs **full**
coverage, which adds every benchmark, the isolated samples and the oracle-cache
audit. Plain coverage, every plain run as pushes measured before the nightly
mode, satisfies neither.

A state is measured behind its own plain hosted rows (both architectures,
every shard), the gate a pull request needs before the 9700X. Its sanitizer and
fuzz results come from its pushes and the snapshot's daily run. They are
reported, but they don't block measurement, which was the owner's choice on
2026-11-03.

### Evidence and reuse (`tools/ci/nightly.c`)

Every `9700x.yml` run has an `evidence` job, which must succeed:

- **Pull request:** its draft state alone.
- **Push:** the one merged pull request of this repository whose merge commit
  it is. That pull request's newest run of the workflow must have completed
  within a day, with the 9700X job, the aggregate and every plain row green.
  The run's own coverage record must name that run, the head and a completed
  targeted measurement of the identical tree.

  Tree identity carries everything the repository pins: the workflow, the
  inventory, the toolchain's and corpus's digests, and the harnesses'
  commits. The build and the inventory read no Git metadata; the self-test
  fails if they start to.

  What the repository doesn't pin is the host. Every 9700X job records it as
  its host identity, made of:

  - the kernel, the microcode, GCC's version and glibc;
  - a digest of the software the native inventory runs, which covers:
    - the package database's every package and version;
    - the contents of each external tool the inventory calls, and of every
      shared library those load. The tools are GCC with its `cc1`, `cc1plus`
      and `collect2`, binutils, `gdb`, `perf`, `valgrind`, `mold`, `wild`,
      `make`, `file` and the shell;
    - the runtime GCC links a static program with (`libc.a`, `crt1.o` and the
      rest, as `test-static`'s `cc -static` resolves them);
    - every file of the packages that own any of those, and of the C
      toolchain's packages (`gcc`, `gcc-libs`, `glibc`, `binutils`,
      `linux-api-headers`): their headers, static libraries, startup
      objects, linker scripts and configuration, by content.

  So an oracle, runtime or header replaced under the same version changes the
  identity. In the nightly mode, such a push runs a
  `host-identity` job on the 9700X. It takes the lock for seconds and reads
  the identity with the same step as the 9700X job; the self-test keeps the
  two steps identical. The 9700X job is skipped only when that identity
  equals the one the pull request's run recorded. A component that can't be
  read leaves no identity at all, never a placeholder that another unread
  one would match. The cases are:

  - a host without a package database;
  - a file that can't be hashed;
  - a package query that fails or prints anything on stderr;
  - an ownership query that doesn't account for every file. Each file needs
    an owner, at the version the package list gives, or pacman's "No package
    owns" and no other message;
  - an owners' file listing that lacks a file the ownership query attributed
    to them.

  A file no package owns is hashed like any other, but a failed query is
  never taken for one. The warning names the check that failed.

  The reuse accepts only an identity with every component present. A
  different identity, an incomplete one, a record without one, or a failed
  `host-identity` job runs native correctness after all. The record then says
  `host-unconfirmed`.

  Measurements are reused too only when the first-parent tree is also
  identical; otherwise they are the backlog's. Missing, ambiguous, stale or
  mismatched evidence, including an API failure, runs the work.
- **Nightly dispatch:** the commit must be a first-parent state of main at or
  before the pinned snapshot, or the job fails and nothing runs. The newest
  conclusive push or dispatch run of that commit decides its hosted rows: a
  pass is reused and a later failure isn't hidden. The window must also admit
  the run.

Every run's `record` job writes `coverage-record-ATTEMPT`, kept 90 days. It
holds the commit, tree, first parent and first-parent tree; the blobs of the
workflow, selector, inventory, toolchain and corpus scripts; the selection; what
the run ran, reused, admitted and deferred; the 9700X job's window, native
correctness and host identity; and the outcome. Outcomes are `complete`,
`not-required`, `deferred` (no hardware admitted), `pending` (a push qualified
by reuse whose measurements weren't reusable), `blocked-hosted`,
`blocked-native`, `failed` (a measurement after native correctness),
`interrupted` (cancelled or timed out) and `rejected`. Only the 9700X job's
success, or a push's evidenced reuse, completes.

### The coordinator (`.github/workflows/nightly-backfill.yml`)

**Scope.** The backlog's states start at the epoch: the first-parent commit of
main that added the nightly worker (`tools/ci/nightly.c`). The worker is each
state's own `9700x.yml`, dispatched with a `backlog` or `snapshot` purpose and
checking its window after the lock. Earlier commits' workflows have neither, so
the coordinator can't measure them within the window, and they are out of
scope. Their coverage is what their pushes and the old daily run measured. The
report counts the fortnight's first-parent states before the epoch.

The coordinator is hosted, and only main's scheduled and dispatched runs write.
They create and delete `nightly/SHA` branches, and dispatch and cancel
`9700x.yml` runs on them, so that each run, its checks and its definition are
that state's own. Its run name, `Nightly PURPOSE (admit|defer) of SHA
[REQUEST]`, correlates it with the ledger.

1. **Harvest.** Each step reads a fortnight's runs (later steps a day's) and
   harvests the records of completed runs the ledger lacks. A record that
   disagrees with the API's job conclusions counts as nothing.
2. **Pin.** The cycle (the night ahead, from noon) pins main's head as its
   snapshot once. Every dispatch of the cycle names that pin, not main's head
   at dispatch, which may have moved on. The worker checks the state's
   first-parent ancestry against the pin and records it.
3. **Prepare.** Before the window, the coordinator dispatches the snapshot's
   deferred run: every hosted row and the 100,000-input fuzz campaigns, the
   daily diagnostics the old schedule ran. It also dispatches the plain rows of
   the oldest states that have none, three at a time and forty a cycle. A
   prepare job that waits for the window plans again every five minutes until
   it opens, so each completed preparation frees its slot for the next. It
   never admits hardware, even when a step lands at 04:00 or carries an
   override: admission is the window job's alone. The window job starts from
   the prepare job's ledger and its pending dispatches, so a dispatch the API
   doesn't show yet isn't repeated.
4. **Measure.** Inside the window it admits one 9700X run at a time: the
   snapshot's comprehensive run first, then the oldest ready state. While the
   snapshot's hosted rows are in flight, it keeps room for the snapshot. If
   the snapshot is ineligible, it measures the newest eligible earlier state
   and says so. The comprehensive run is a daily requirement: only one
   completed in this cycle meets it. An unchanged main still gets its daily
   full run and oracle audit, though an earlier cycle measured the same
   commit.

States move between `not-required`, `complete`, `in-flight`, `ready`,
`preparing` and `needs-preparation`; `blocked-hosted`, `blocked-native` and
`failed` (three failed or interrupted measurements) are reported and skipped,
and never block later states. Nothing depends on dates but the cycle, so
unfinished states carry over, and a completion never moves a cursor past an
earlier gap. Pending dispatches that aren't yet visible as runs prevent
duplicates. A step whose run listing fails, even partly, plans nothing until a
full listing succeeds; otherwise a run in flight it hid would be dispatched
again. An admitted run older than two hours is cancelled.

**Time.** The tz database's Europe/Madrid is checked at three points:

- at dispatch, by the coordinator's planner;
- at admission, by the `evidence` job;
- after the 9700X job's hook took the lock, by its first step after the host's
  own check. That step compares the clock with the cycle's 04:00 and 07:00,
  which the `evidence` job computed as instants. It runs before the checkout,
  the cache and the toolchain's staging, so a job that took the lock too late
  does nothing but refuse.

If the database is unavailable, nothing is admitted. The self-test checks the
database against the EU's rule every hour from 2026 to 2030. A run is admitted
only if its whole bound still ends by 07:00: the job's timeout (20 minutes for
backlog runs, 25 for the snapshot) plus 5 minutes, and the coordinator adds
the hosted jobs' time. A refusal after the lock fails the job before any work,
which the record reads as a deferral. The job's timeout ends anything that
runs over, and the hook then releases the lock. The lock's protocol and its
fairness to agent sessions are unchanged; nightly work is kept out of the day
rather than given priority.

**Late schedules.** GitHub starts scheduled runs late. In the week before this,
the daily run's 07:23 UTC never started before 12:39 UTC. So the coordinator
has six schedules, from 22:07 to 07:17. A run that prepares before the window
waits for it when it opens within 5 hours 55 minutes of the job's start. Any
other run waits in the concurrency group, or reports.

**Override.** Both workflows' manual dispatches take an `override` reason for
deliberate out-of-window work. The window checks report the reason and never
pass it silently. Through the coordinator, `mode: run` with a reason
behaves as follows:

- the planner admits as if inside the window, and its report says why;
- the prepare job starts the window job at once;
- the loop ends when nothing more is admissible;
- each dispatch passes the reason on, and the run's 9700X job honours it after
  the lock.

**The ledger** carries over through the coordinator's artifacts: this run's
prepare job's, then the newest from main's previous runs. It is published to
`buster14a/buster-performance`'s `nightly-ledger` branch (`nightly/main-coverage.tsv`
and `nightly/reports/CYCLE.md`) when the `nightly-ledger` environment holds
`BUSTER_PERFORMANCE_LEDGER_TOKEN`. That token needs contents write access to
that repository alone, and the environment should allow main only. Each run's
own record is the source the ledger is rebuilt from, so a lost ledger costs
only the re-harvest.

**The report** gives each status's count; this cycle's selected, completed,
failed and deferred runs and hosted preparations; reuse; the oldest carried-over
state; the remaining states; and evidence links. It warns when the week's
arrivals exceed the completions observed per cycle, or when the oldest pending
state is over three days old.

**Capacity.** In late October main gained 31 to 82 first-parent commits a day.
Nightly runs are bounded at 25 minutes, but PR jobs took 4 to 5 minutes. A
three-hour window holds about 25 to 30 such runs, so the backlog drains only
where pushes and reuse measure most states during the day. The report's alert
shows when they don't.

**Rollout.** The switch is `vars.BUSTER_NIGHTLY`. Until it is `on`, scheduled
coordinator runs are dry runs: they harvest, plan and report. Pushes measure
every plain run, and the daily `23 7 * * *` run of `9700x.yml` runs as before.
A dispatch with `mode: run` can try the coordinator first. With the switch on,
pushes measure targeted, the coordinator dispatches, and the daily run's
`evidence` job retires it.

**Open items** this repository can't close:

- exact lock-wait and lock-hold times, which need the host's runner hooks to
  record their acquisition;
- the ledger's publication token;
- whether the coordinator's capacity drains the observed arrivals, which only
  enabled cycles will tell.

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
The sanitizer rows' job limit is 360 minutes and each command's is 7200 seconds:
the driver's in-process checks run the whole compiler per fixture and take tens
of minutes each with a detector, while the plain rows have a 120-minute job
limit and 600-second command limit.

| `BUSTER_SANITIZER` | Checks |
| --- | --- |
| `none` (default) | Ordinary optimized build |
| `address` | ASan, UBSan, bounds, leaks, stack use after scope and return |
| `undefined` | Independent UBSan and bounds run |
| `thread` | TSan across the correctness inventory, but the compiler's own `main.c` checking comparison |
| `memory` | MSan with level-two origin tracking across the correctness inventory, but the compiler's own `main.c` checking comparison |

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
The other resource-exhaustion fixtures retain their cases and output/fallback comparisons.
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
Under ASan, MSan and TSan, the units driver's walk that fails each reservation in
turn is not run: each detector maps shadow of its own, whose high-water mark the
parent's `VmPeak` keeps above its `VmSize`, so a probe inherits a peak that hides
the growth a limit would fail at, and a limit below the shadow stops the detector
at its start. The parser driver's reservation check still runs in those builds,
with their injected arena limit in place of `RLIMIT_AS`; UBSan and ordinary
builds, whose peak is the run's own, still run the units walk with the OS limit.
TSan also kills a child that starts a thread after a fork from a process of
several threads, so under it `test-lanes`' child forked from a session's lane 0
(#905) runs a group of one.

The TSan and MSan rows leave out the checking comparison of the compiler's own
`main.c` (#129, `tools/ci/correctness.tests`). The unit's 6,323,682
preprocessed tokens make the comparison's two paths hold 7.5 GiB resident
unsanitized (measured with an `-O3` UBSan build on the 7940HS), and TSan's
shadow took the same check past 20 GiB before it was stopped; that cannot share
a 16 GiB hosted runner with any other command. The `-c` commands peak at
10 GiB under TSan and 6.6 GiB under MSan (`-O3`, `--lanes 1`), so those rows
run them in the workflow's separate "Compile main.c alone under TSan or MSan"
step after the suite, where the runner is otherwise idle; the plain, ASan and
UBSan rows, both host architectures, and the 9700X run them in the suite. The
plain, ASan and UBSan rows and the 9700X also run the checking comparison, so
the self-check remains required wherever the runner has the memory. Every other
command runs in every row.

`tools/ci/sanitizers.sh` replaces inherited runtime options with a fixed fatal
policy and matching symbolizer. Deliberate faults prove each enabled detector,
its failure exit code and source symbolization before the full suite starts.
These proofs use a separate directory. An unconditional report audit rejects
diagnostics from the actual suite even when a negative test expected its child
to fail. The libFuzzer crash-replay proof's planted abort writes its stack traces
to a directory of its own, and each one must name the probe's source. No
suppression file is used, and the only command a row leaves out is the `main.c`
checking comparison above, for its measured memory; `parallel` reports it as
skipped and counts it apart. Reports, detector proofs and compile commands are
retained as workflow artifacts.

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
