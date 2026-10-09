# Clang analyzer shards

`build.c` owns this gate through `tools/clang_analyze.c`. The authoritative
input is the selected CMake `compile_commands.json`, with its original working
directories, compiler and configuration. It never discovers source files by
walking the checkout or synthesizes compilation flags from a module list.

The ordinary `clang_analyze` command and Ninja target now prepare a complete
manifest, launch independent native shard workers, and aggregate their results.
The existing optimized unity gate remains: a unity TU is indivisible. CI also
requires **Clang analyzer shards**, using a Clang Release, unsanitized,
non-unity tree so the split source graph is actually exercised. This job runs
independently of the desktop combinations; `CI complete` requires it.

## Reproduction

Bootstrap with `build.sh`/`build.ps1` as usual. Keep a configured tree and all
its sources and generated inputs unchanged until aggregation finishes. Generation
owns the build tree and must not overlap a reader. For a split analyzer tree:

```sh
./build.sh generate --build-directory build/analyzer-tree --ci --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF
./build.sh clang_analyze build/analyzer-tree --config Release --shards 8 --jobs 2 --results build/analyze-full
```

`--results` must name a fresh directory with an existing parent. Without it,
the driver creates a unique directory beside the database. Default limits are
eight shards, at most two simultaneous workers (also bounded by host CPU count),
and 600 seconds per TU. For eight shards the native coordinator launches in
measured priority order `2,7,3,0,6,5,1,4`; other shard counts retain numeric
order. This changes admission only, preserving shard IDs, module ownership,
manifest contents and each worker's original TU order. Available worker slots
are refilled as shards finish. `ANALYZE_DISPATCH ordinal=... shard=...` records
the real launch order, including under `--quiet`. Each worker runs one
analyzer at a time, keeps going
after failures and reaps every child. `--timeout` changes the per-TU bound;
`--jobs 1` uses the same worker path with serial scheduling. The standalone
`--clang` override remains available for analyzer selection and test oracles.

Independent execution uses the same database and options at every step.
Bootstrap once, then invoke the built driver so parallel workers do not
concurrently overwrite the bootstrap executable:

```sh
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --prepare
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --shard 3
# Run the other zero-based shard indices in separate processes, then:
build/build clang_analyze build/analyzer-tree --config Release --shards 8 --results build/analyze-manual --aggregate
```

A single worker reproduces its own shard, not the complete gate. A second worker
claiming the same shard directory fails. To repeat it, prepare a fresh run.
`--shards 1 --jobs 1` exercises full coverage through one worker.

## Coverage and evidence

The manifest retains the complete original database, configuration, analyzer
projection, module assignments and run directory. SHA-256 binds each shard
report to that exact manifest. Configuration/language exclusions are counted;
malformed entries, unusable commands, duplicate TU identities and empty selected
coverage fail before work starts. The module key is the source basename without
`.c` or `_test.c`; fixed FNV-1a maps that key to a shard. Source/test pairs stay
together and assignments do not depend on database order or completion order.
Identical basenames in different directories share a shard but keep distinct TU
identities. Empty shards publish an explicit empty report.

Projection replaces the compile operation with `--analyze`, uses text
diagnostics and removes object/dependency output options. All semantic arguments
are retained in order, including build-host definitions previously discarded by
the old gate. Windows command-field decoding preserves escaped quotes and paths
with spaces. Commands must explicitly name the selected source. Response files
are rejected because they could override the projected action; they are not
emitted by the canonical database. The manifest records both original and
projected arguments.

Every TU gets a diagnostic log and one terminal row in its shard report,
including a SHA-256 of the log. Missing or changed logs fail aggregation too. A
warning on either output stream fails even with a zero exit status. Nonzero
exits, crashes, deadlines, launch failures and evidence write failures fail with
the source/shard named. Aggregation checks every expected shard and TU, rejects
duplicates, unexpected indices, wrong assignments, stale fingerprints, truncated
or extra rows, and reports omitted files. Worker failure does not suppress
other workers or the final accounting. A report is published only after the
worker finishes; absence is never interpreted as a clean analysis.

There is **no cross-run analysis cache**. Every new run reconstructs the selected
TU obligations and analyzes them with the current inputs, including transitive
and generated headers. Only exact rows with a successful same-run context proof
may share one analyzer launch; each row still gets its own checked result and
log. A change in a later run is always analyzed afresh. Results are retained
evidence, not permission to skip subsequent work. The fingerprint proves
command/coverage identity within an immutable run; it is not an incremental
provenance proof for a changed checkout or analyzer installation.

## Same-run exact invocation groups

Plan schema `BUSTER_CLANG_ANALYZE_PLAN_V2` keeps every selected compilation
database row and adds a representative index for execution planning. Row
identity remains `(working directory, source, output)`; execution identity uses
the exact ordered projected analyzer argv plus the exact source, working
directory and shard. Output-only differences can therefore share a
representative, while changed definitions, include order, target flags, cwd,
compiler spelling or any other argument remain separate. The representative is
chosen by bytewise `(directory, source, output)` order, independent of database
order. Hashes are evidence labels; the planner byte-compares the relevant
strings before grouping.

Grouping is opt-in per equivalence class and conservative. On Linux it requires
the original compiler argument to be an absolute supported Clang path, a
supported environment, and a successful context proof. The planner keeps the
original argv unchanged because argv0 can select driver mode and defaults.
Direct ELF executables and bounded symlink chains are supported. The proof
binds each compiler path component, symlink target and final ELF target, then
rechecks that path identity with the compiler content before and after
execution. It also binds exact version/resource-directory queries, the complete
process environment (sorted bytewise; excluding the shell-maintained `_`
command marker), runtime library content and the resolved `ldd` closure.
This proves stability for the caller's trusted installed Clang; ELF format,
name and version output do not authenticate a compiler publisher.
The closure query is replayed at both context checks so a changed loader lookup
invalidates the representative. Compiler ancestor directories bind device,
inode, mode and ownership; unrelated membership timestamps do not make a
result-file sibling invalidate the path proof. Include and default-config trees
still receive full recursive membership snapshots. Compiler, runtime-library
and regular symlink-target contents use bounded streaming SHA-256 reads with
descriptor and path metadata checks before and after each read. Positive `-M`
dependencies are content-hashed and rechecked. The effective analyzer command printed by Clang's `-###` driver
expansion is also bound, so automatically discovered config flags are
represented even when they do not change preprocessed text. Default config
search trees are snapshotted; explicit config files, config environment
overrides, and effective plugin loads keep rows independent. Raw `-Xclang` and
`-Xanalyzer` forwarding is unsupported. The expanded cc1 command also rejects
analyzer checker configs, AST merges, PCHs, modules, profiles, overlays and
plugin loads, including when a default config injects them; these options can
refer to external state that dependency scanning does not observe. Symlink
cycles, unsupported targets and unstable paths keep rows independent.

The proof records Clang's verbose include-search directories and the parent
directory of every positive dependency, covering quoted and forced includes
whose parent is outside configured search roots. Directory names are sorted
bytewise before hashing; recursive snapshots record search-tree membership,
file kinds and metadata, symlink targets and regular-file symlink contents.
This captures additions and removals that could change a relative negative
include lookup. The exact preprocessing output is content-hashed and replayed
at both context checks, which also catches changes to absolute or escaping
`__has_include` queries even when they produce no positive dependency. Input,
directory, preprocessing and driver-expansion state is checked before and after
the representative executes. Unreadable, special, unstable or unsupported
inputs keep their rows on independent executions.

These checks run under a caller-managed immutable-run assumption; they are not
filesystem locks. Source, header, search-tree, compiler, runtime-library and
configuration inputs must not be generated, replaced or otherwise mutated
concurrently from planning through postflight. A transient change to an
out-of-tree negative lookup that is restored between checks is not ruled out.
The proof establishes observed stability under that contract; it does not
authenticate the compiler publisher or guarantee equivalence for every possible
external mutation schedule.

Dependency and dynamic-builtin probes run with `--analyze` while selecting the
final `-M` or `-E` action. This preserves analyzer-specific predefined macros
such as `__clang_analyzer__` in conditional includes and preprocessing state.
Dynamic date/time builtins are detected by a real Clang preprocessing probe
that neutralizes inherited general `-Werror`, then adds
`-Wdate-time -Werror=date-time -Wsystem-headers`, so token-pasted builtins are
covered without failing on unrelated system-header warnings. The probe also
rejects `-w`, active `-Weverything` pragmas, and date-time diagnostic pragmas
that ignore or downgrade that warning, including expanded `_Pragma` forms; an
unrelated warning pragma does not block grouping. Ordinary macro token pasting
remains eligible when it does not synthesize a dynamic builtin. Compiler
wrappers, unbounded or cyclic symlink chains, plugin loads, modules, PCH,
profile/coverage state, injected config, unstable path search and non-Linux
platforms execute every selected row independently. These restrictions can
lose an optimization opportunity; they never remove a source obligation. The
same-run proof is not a cross-run producer identity or a reusable cache
receipt. Every new invocation still starts with fresh analysis. Ordinary run,
prepare, worker and aggregate planning/proof, worker preflight and worker
postflight durations are reported separately. An ordinary run prints
`ANALYZE_PLAN mode=run` after successful planning; terminal
`ANALYZE_RUN elapsed_us` includes that setup time. The environment proof stores sorted
variable names and value digests, never environment values; its diagnostic can
name a changed variable without printing its value. The extra preprocessing
and driver-expansion probes add planning and context-check cost. A matched
two-row O0 diagnostic measured 131.190 seconds for the candidate versus 0.287
seconds for the baseline; the retained evidence attributes much of that cost
to runtime-closure replay and hashing. This small probe does not measure the
net effect on the full analyzer workload.

Schema `BUSTER_CLANG_ANALYZE_RESULT_V2` records selected rows, actual launches
and aliases separately. Every row has one terminal status, representative
index, launched bit, duration and diagnostic-log digest. An alias gets its own
row and byte-identical copy of the representative's output log; representative
warnings, failures, crashes, timeouts, launch errors, context changes and log
write errors therefore fail each affected row. Independent aggregation
rebuilds the plan from the authoritative database, verifies the representative
mapping and execution totals, and checks every row's status and log, including
the alias-to-representative byte comparison. Manual prepare/worker/aggregate,
serial reproduction and the worker-budget reader use the same V2 contract.
Each worker independently rebuilds all selected rows, original and projected
arguments, shard assignments and invocation classes. It re-proves and exactly
compares the full proof record for each class it owns before launch; other
shards' proof bytes are parsed only to validate the bounded mapping and never
choose work in that worker. The result digest still covers the complete stored
manifest. Aggregate independently rebuilds and compares the complete proof
manifest before validating every shard result.

Rollback of the complete change restores the prior V1 producer and behavior.
Frozen V1 and V2 evidence keeps its original meaning; readers reject a schema
version they do not support. Within the current implementation, any row whose
context proof is ineligible or fails remains its own representative, so V2
reports `unique_executions == selected_rows` and zero aliases for that row set
without changing coverage. No speedup is claimed from the fixed-duration
historical replay; performance validation remains incomplete until approved
Zen 5 workload evidence is available.

## CI controls and measurements

The matched O0 cost probe, original 182-row / 135-group / 47-repeat derivation,
final reader suite, and failed diagnostic attempts are retained in
[`docs/performance-audits/evidence/2026-10-09T0238Z-issue3130/README.md`](performance-audits/evidence/2026-10-09T0238Z-issue3130/README.md).
The O0 result is a performance warning, not a speedup claim. No approved Zen5
candidate-versus-baseline run is available, and this cloud's descendant
process/RSS sampling is incomplete; full-workload performance acceptance
remains open.

`./build.sh clang_analyze --self-test` compiles a small native process oracle and
exercises warnings on both streams, nonzero exit, crash, timeout, large concurrent
stdout/stderr output, failed launch, continued coverage, missing/duplicate/stale/
malformed reports, changed configuration, duplicate workers, invalid/empty
inventories, independent prepare/worker/aggregate, and a one-shard run. A real
Clang control checks relative includes and confirms a changed transitive header
is reanalyzed. Every desktop combination entrypoint and the independent analyzer
job run these controls.

The required analyzer job executes **one complete candidate analysis**, followed
by independent aggregation of its retained results. Pull requests, merge groups,
main/tag pushes and manual runs all use that same path. Changes to `build.c`, its
dependencies, the analyzer or the workflow cannot select a reference pass.
There is no `--baseline-driver` CLI option. The `analyzer_comparison` dispatch
input is retained only because the frozen native-retirement support declaration
pins `tests/ci_tools_test.py`, which asserts its declaration; dispatching it as
`true` fails the candidate bootstrap before any analysis. Removing the input
needs a separate support-contract successor. Removed comparison options are
rejected rather than silently ignored. Existing
exact queue-to-main reuse remains separate: a valid reuse receipt can replace
fresh execution, but never an incomplete candidate run.

Negative controls print their rejection evidence on purpose, so the self-test
scopes it. Each operation-running check first prints
`ANALYZE_SELF_TEST_BEGIN name=... expect=reject` (or `expect=accept`); its
`ANALYZE_SELF_TEST name=... status=...` verdict closes the scope. Inside a
rejection scope the driver's diagnostics read `expected-error:` and failed
`ANALYZE_UNIT`, `ANALYZE_SHARD`, `ANALYZE_AGGREGATE`, `ANALYZE_RUN` and
`ANALYZE_WORKER_SAMPLE` records end their status with ` expected=1`. Shard
workers launched inside the scope receive the internal `--expect-rejection`
argument, which is accepted only with `--shard` and changes text, not results.
Raw analyzer logs, `+` command lines, shard reports and unit logs are
unchanged; a self-test `run.txt` or `qualification.txt` repeats its printed
record. An accept scope, an unscoped check, a real run and a failed check keep
plain `error:` and `status=fail`; a failed check also prints
`error: analyzer self-test check failed: name=...`. The final
`ANALYZE_SELF_TEST_RESULT` line (`checks`, `expected_rejections`, `failures`,
`evidence`, `status`) is the self-test verdict and decides its exit status. The
real analysis verdict remains the unqualified `ANALYZE_RUN` and
`ANALYZE_AGGREGATE` records of the split-tree run.
`python3 tools/analyzer_worker_budget_test.py -v` checks that a passing
self-test prints no unqualified failure, that qualified lines appear only in
rejection scopes, and that a failing real campaign still prints plain `error:`.

`Bootstrap and identify candidate build driver` compiles one driver with Clang's
`-MMD` dependency output. The existing `tools/analyzer_reference.py` helper now
owns candidate provenance only: it verifies compiler-selected repository inputs
against their Git blobs and binds the commit/tree, dependency/policy hashes,
executable SHA-256, resolved Clang identity/version, exact bootstrap command and
hashed compiler/driver environment. Missing policy inputs or incomplete provenance
fail bootstrap. No historical source tree or reference executable is created.
The module name and its read-only `load_selection` decoder remain for historical
source-pinned measurement readers; the comparison selector/materializer and their
CLI commands have been removed.

`Analyze candidate and aggregate all module shards` independently rebuilds that
manifest immediately before launching the candidate and requires byte equality
with the bootstrap record. Missing, symlinked, changed or stale evidence, source,
executable, Clang or environment fails before analysis. Every event binds the
executable context. This verifies the actual candidate, not equivalence to any
other driver. The native analyzer's command projection, selected TU set, worker
budget, deadlines and failure controls are unchanged; same-run representative
mappings and per-row outcomes are bound by the V2 manifest and results.

The workflow then invokes `clang_analyze` once for execution and once with
`--aggregate` to independently verify the reports. Aggregation does not launch
Clang. `ANALYZER_POLICY candidate-only-v1` makes the policy visible in the job
log; `ANALYZE_RUN` records the complete candidate wall microseconds. No reference
result or `ANALYZE_BASELINE` success is fabricated. Historical comparison logs
retain their original meanings and are never relabeled as candidate-only data.
The frozen #2610/#2119/#2120 population reader still requires its original source,
producer blobs, dispatch inputs and selection-record schema; this removal does
not amend or qualify that historical campaign. Replay it at its pinned revision.
The generic timing reader accepts either historical or current campaign step
names, refuses missing/duplicate campaign steps and keeps different workflow
blobs in different measurement cohorts.

`peak_pending_workers` is launched-but-not-yet-reaped worker concurrency;
actual analyzer overlap can be lower, particularly for empty or tiny shards.
On Linux the coordinator and descendants are sampled every 25 ms. The sampler
resolves its root through `/proc/self`, then follows the child list for each
process's main thread at `/proc/PID/task/PID/children`; it does not claim to
enumerate children created by non-leader threads. `peak_live_processes` and
`sampled_peak_tree_rss_bytes` retain the observed process count and summed RSS
as lower bounds, including when a later sample finds that discovery was
incomplete. Shared resident pages count in each process; sampling and normal
exit races mean these values are not an atomic process-tree snapshot, PSS, or a
physical-memory measurement.

Every `ANALYZE_RUN` also reports
`process_tree_status=complete|incomplete|unavailable` and a stable
`process_tree_reason` token. `complete` means a bounded traversal successfully
read `statm` and the main-thread child-list interface for every live PID it
examined at that sampling instant. It does not cover processes that start or
exit between samples, children created by non-leader threads, or arbitrary
workloads outside that bounded traversal; the values are not a continuous,
all-thread process-tree snapshot. A missing child list while its process is
live, malformed child data, unreadable live-process memory, the 4096-PID table
limit, the 8192-read child-list budget, or unknown parentage when `stat` and
parent-list evidence are unavailable marks the run `incomplete`.
Membership checks stop as soon as they find a PID and report an unknown result
if the bounded read cannot prove absence. A normal exiting or reparented PID is
treated as a sampling race when procfs confirms it is no longer a live child.
Unknown-parentage PIDs are omitted from the numeric RSS lower bound.
Other hosts report `unavailable` with
`process_tree_reason=unsupported-host`. A Linux run with no resident samples
reports `unavailable`; its reason keeps the first sampling obstacle, or uses
`process_tree_reason=no-rss-samples` when no other obstacle was observed.
`ANALYZE_RUN status` remains the analyzer result and is independent of this
sampling status. The worker-budget verifier requires
`process_tree_status=complete` for every arm and rejects a missing status even
when the numeric lower-bound fields are positive.

`ANALYZE_SHARD` and `ANALYZE_AGGREGATE` record the largest child high-water RSS
from POSIX `getrusage`, in bytes. It is **not a sum of simultaneous process-tree
RSS**. Windows reports zero for unavailable RSS. Compare wall time and the same
eligible TU count alongside these memory/concurrency limits; no platform-wide
speedup follows from a single hosted-runner sample. CI retains the revision,
Clang version, database, CMake cache, manifest, shard reports and logs.
The initial complete comparison and its measurement limits are recorded in
[the CI performance audit](performance-audits/2026-09-12T192036Z.md).
The later exact-command investigation of the four analyzer hotspots is recorded
in [the #3131 profile audit](performance-audits/2026-10-08T225442Z.md). It is
pinned to source revision `96eba05bf1be5a146afc7ca3a1d1c6ae2fac9473`; it found
no supported safe source reduction and changed no checker, source coverage,
scheduler, or CI gate. Its cloud timings are diagnostic, not approved-host
performance validation.

`python3 tools/analyzer_selection_test.py -v` executes the actual bootstrap and
campaign bodies with a logging compiler/driver. It covers each event, changed
root/transitive/analyzer/workflow inputs, one bootstrap and one candidate
execution, independent aggregation, stale/tampered provenance and fatal candidate
or aggregate failures. The native `clang_analyze --self-test` additionally rejects
both spellings of the retired reference option and retains all real child and
coverage controls.

A deliberate analyzer performance comparison can invoke separately built native
drivers explicitly outside required CI. Such an experiment is not a correctness
prerequisite and cannot automatically reinstate a reference pass. Removing the
measured reference work reduces this job's work; it does not establish the same
whole-CI latency saving when another required job controls completion.

## Eight-shard launch priority

Two completed Ubuntu Clang 21.1.8 jobs with 182 eligible/checked units and zero
failures had the same shard-duration ranking:
[job 113505844135](https://github.com/buster14a/buster/actions/runs/37833871097/job/113505844135)
and [job 113480896888](https://github.com/buster14a/buster/actions/runs/37826583840/job/113480896888).
Their complete candidate walls were 907.196 and 1194.076 seconds.
Replaying their observed shard durations with two slots gives 906.702/1193.412
seconds for numeric admission versus 834.650/1100.619 seconds for the measured
priority. This is a fixed-duration scheduling hypothesis: the executions used
different source revisions and VMs, and changing overlap may change durations.
It is not an executed A/B speedup or qualified Zen 5 performance validation.

The native scheduler keeps two workers and starts the largest measured shards
early. No source, checker, warning policy, timeout, immutable-run proof or
independent aggregation is removed. Native self-tests check a complete
permutation for every supported shard count, numeric fallback, reordered
successful coverage, complete results after a warning and independent replay.
Revisit priority when the source inventory or measured relative weights change.
Rollback is to return the ordinal in `clang_analyze_schedule_shard`; it needs no
CLI, workflow or evidence-format transition. Qualified performance validation
remains incomplete until relevant approved-host evidence exists (#2761).

## Opt-in worker-budget qualification

The routine default remains two workers. `--qualify-workers` runs four complete
candidate-only inventories sequentially in fixed `2,4,4,2` order through the
ordinary native scheduler. It requires at least four reported host logical CPUs
and four shards, and refuses `--jobs`, independent worker/prepare/aggregate modes,
and self-tests. Host logical CPU count is only a preflight;
affinity, quota, physical topology and memory still need independent inspection.

```sh
./build.sh clang_analyze build/analyzer-tree --config Release --shards 8 --timeout 600 --quiet --qualify-workers --results build/analyzer-budget
python3 tools/analyzer_worker_budget.py host build/host-context.json
```

The result directory contains one fresh manifest, shard reports, diagnostic logs
and `run.txt` per arm, plus `qualification.txt`. All arms run even after an
earlier failure; any failed arm or changed database/command inventory fails the
campaign. The only intended manifest difference is the arm's output directory.
`children_cpu_us` is the POSIX waited-descendant user+system CPU-time delta,
including workers and their reaped analyzers; zero means unavailable. It is
separate from shard wall times and hosted VM occupancy. Neither CPU time nor
sampled summed RSS proves physical-core availability or physical-memory use.

The manually dispatched `Analyzer worker budget qualification` workflow runs
two independent `ubuntu-26.04` trials, with one frozen driver and authoritative
split database per VM. It retains source/tree and binary hashes, Clang identity,
CPU affinity/topology, cgroup ancestor limits/counters, host memory/pressure,
all failures and full per-TU evidence. `tools/analyzer_worker_budget.py verify`
independently checks the selected union, exact commands/deadlines, all shard
fingerprints, diagnostic checksums, and the explicit complete process-tree
status for every arm. It supports downloaded, relocated artifacts and never
changes a default or admits a performance result. Run
`python3 tools/analyzer_worker_budget_test.py -v` after building the native
driver at `build/analyzer-driver`; `BUSTER_ANALYZER_TEST_DRIVER` overrides that
path. Native self-tests cover a positive synthetic descendant tree, a readable
coordinator with a missing child-list interface, a child removed from its
parent's readable list, a zombie exit race, capped duplicate-heavy child lists,
and live children with missing `statm` or child-list files. The worker-budget
verifier rejects incomplete or absent status despite positive RSS fields. A
live host qualification control runs only when the host provides complete
sampling evidence.

#2033's predeclared decision requires at least 10% wall improvement in each
order-balanced pair on both trials, no more than 5% additional child CPU work,
identical successful coverage/diagnostics, four CPUs of affinity/quota and
sampled summed RSS below 25% of the smallest known host/cgroup memory budget,
with no new OOM/swap pressure. Missing budgets or inconsistent evidence are
inconclusive. A benefit in analyzer VM occupancy is not a whole-CI speedup;
compare the other required jobs and report bounded timestamp replays separately.
Rollback of the experiment removes the opt-in workflow, reader/tests and
qualification option; the ordinary one-worker reproduction and indivisible
unity TU keep their existing path.

## Split-analysis contracts

The first complete split run found paths hidden by unity analysis. The gate
keeps every checker enabled and every warning fatal. The accompanying source
changes short-circuit varargs validation before reading missing types, decline
layout parsing without its parser context, preserve empty-copy validity and
check optional backend type/CFG storage. Tests no longer dereference failed
lexer outputs or invalid register indices after recording an assertion failure.
Existing internal invariants are explicit at their consumers: list endpoints
are published together, index arrays exist for counted nonempty ranges, local
rows and symbol rows share allocation, and metadata helpers retain value
storage. These checks document the existing validated-input contracts rather
than suppressing analyzer reports.
The lexer's final emitted lane comes from the existing contiguous low-bit mask
using a fixed one-bit shift. This keeps the end-of-window computation defined
without relying on the analyzer carrying a range proof for a variable shift.
