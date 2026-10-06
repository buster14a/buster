# Admission for the direct 9700X workload workflow

The Ryzen 7 9700X is not a general Actions executor. Exactly one workflow may
reach it: `.github/workflows/9700x-direct-bench.yml`. It compiles and runs the
owner's own pull-request workloads (#2704). It compares the compiler of an
owner pull request with its merge base on request (#2769), and the compiler
of each main merge-queue candidate with its predecessor's (#2752). Its hosted
markers, `.github/workflows/9700x-direct-request.yml` and
`.github/workflows/9700x-compiler-request.yml`, never select the runner. The queued benchmark service
and its dispatch workflow are removed (#2708). No other workflow may select
the runner, and `tools/bench_direct/workflow_policy_test.py` fails the required
"Benchmark service workflow policy" check if one does. That check keeps its
historical name because the `main` ruleset requires it by name.

## Gate

`.github/workflows/9700x-direct-bench.yml` starts on `workflow_run`, when
`.github/workflows/9700x-direct-request.yml` completes. The request workflow
runs on `pull_request` for changes to `benchmarks/9700x/*.c`; it is a hosted
marker job that checks out nothing and holds no capability. There is no manual
dispatch and no approval. `workflow_run` executes the bench workflow's
definition from `main`, so a pull request cannot edit the gate and the run
matches the runner group's `@refs/heads/main` pin. An edited copy of the
request workflow in a pull request gains nothing, because the gate reads who
started that run from GitHub's record. A `push` or `pull_request` trigger on
the bench workflow itself would run the branch's own copy and must not be used
for this host; the repository-wide ban on `pull_request_target` stays intact.
To run a workload from a branch, open a pull request from it; a draft is
enough.

Both jobs require the repository variable `BENCH_DIRECT_ENABLED == 'true'`, a
successful request run for a `pull_request` event whose head repository is
this repository, and `davidgmbb` by login and numeric ID 39247043 as that
run's actor and triggering actor. A re-run of the bench workflow must also be
triggered by `davidgmbb`. The hosted `authorize` job checks out only `main`'s
`tools/bench_direct` and runs `authorize.py`, which re-reads the request run
through the API and requires exactly one open pull request for its head
commit, authored by `davidgmbb`, with head and base in this repository. Only
then does it emit the run attempt and the pull request's base commit, and
`bench` is bound to an authorization from the same attempt. A request from any
other account, from a fork, or for another author's pull request skips or
fails before the self-hosted runner. GitHub reuses the outputs of jobs that
succeeded in an earlier attempt, so a maintainer re-run
must use **Re-run all jobs**. An agent that pushes with the owner's
credentials is the owner for this gate.

`bench` receives no token capability, no secret and no environment. It checks
out `main`'s `tools/bench_direct` as the trusted harness and the pull request
head's `benchmarks/9700x` as data, both without persisted credentials, then
runs only `trusted/tools/bench_direct/run_workloads.py`. The pull request
supplies C source that is compiled and executed as the runner account. That is
the intended capability, and it is why the author gate is the whole control:
there is no containment, host lease or sealed result. Do not widen the author
list.

`authorize` also reads the pull request's changed files. `bench` runs only
when a workload or its `.data` file changed. `compare-pull` and `publish-pull`
run only when `benchmarks/9700x/compiler-compare.request` was added or changed.
For that comparison `authorize` resolves the merge base with the base branch
and both trees from GitHub's records. `compare-pull` has the same
restrictions as `bench`, but checks out the whole pull request head (full
history, contents on demand), because it builds the compiler at the merge
base and at the head. That executes the pull request's build as the runner
account under the same owner-only gate. `publish-pull` is hosted and is the
only job of that path with `checks: write`. Its check name,
`9700X compiler benchmark (pull request)`, and marker,
`buster-9700x-compiler-pr-v1:<head>`, differ from the queue's, so admission
never reads it.

Every compiler receipt must record the observed CPU model of the host that
measured it. The harness refuses to measure, and the publisher refuses to
accept, a receipt whose CPU is not the AMD Ryzen 7 9700X (#2761).

The actor restriction governs who starts the workflow, not who edits its
definition. Changes to the workflow, its harness or this policy test need
owner review before they reach `main`.

## Merge-group compiler comparison

`9700x-compiler-request.yml` runs on `merge_group: checks_requested` for every
main queue candidate, without a path filter. It is a hosted marker with no
permissions and no checkout; its completion starts three jobs of the bench
workflow from `main`, only while `BENCH_DIRECT_ENABLED` and
`BENCH_COMPILER_ENABLED` are both `true`:

- `authorize-compiler` (hosted, read-only) runs `authorize_compiler.py`. The
  queue starts merge-group events, so the run's actor is not the authority.
  The authorizer re-reads the request run (`merge_group`, success, this
  repository, the queue branch), the live queue ref (it must still name the
  head), the group commit (exactly two parents: the queue base, then the pull
  request head), the base commit and both trees. It also re-reads the pull
  request named by the queue branch: open, targeting `main`, head and base in
  this repository, head equal to the second parent, and authored by `davidgmbb`
  by login and numeric ID. Any other author or a fork is refused before the
  runner. That keeps the direct path's trust boundary: only the owner's code
  runs on the host.
- `compare` (the 9700X, no token capability) checks out `main`'s `tools` and the
  group head with both parents, without persisted credentials. It then runs
  `compiler_compare.py`. That harness reads the queue ref anonymously first and
  marks a replaced or removed group as superseded without building. Otherwise
  it builds tests-off Clang Release `ide` binaries of the base, then the head,
  then the base again for the frozen workload's generated closure. Finally it
  runs `tools/uarch_lab.py compare` with the frozen `compiler-compare-v1` profile
  (`compiler_receipt.PROFILE`). The evidence artifact
  `buster-9700x-compiler-<head>-<attempt>` keeps the receipt, the lab's raw
  pairs, metadata and `summary.json`, and both CMake caches for 90 days. It
  drops compiled outputs, per-run binary copies and perf data. The candidate's
  build runs as the runner account before measurement. The receipt is
  therefore evidence produced under the owner-only boundary, not a sealed
  result.
- `publish-compiler` (hosted, `actions: read` and `checks: write`) runs
  `compiler_publish.py`. It reads that artifact through the API as bounded
  data, requires its identities to equal this attempt's authorization, and
  re-derives validity from the lab's own `summary.json`. It then creates one
  completed check run, `9700X compiler benchmark`, with external ID
  `buster-9700x-compiler-bench-v1:<head>` on the group head:
  - `success`: a valid core measurement, faster, slower or not detectably
    different.
  - `neutral`: superseded before measurement.
  - `failure`: refused, missing, mismatched or invalid evidence. A failure
    means the candidate was not benchmarked.

  It also publishes after a refusal, so a non-owner candidate shows an explicit
  coverage gap.

The performance verdict is report-only. `BENCH_COMPILER_REGRESSION_POLICY` is
unset or `report-only`; any other value, including a future `enforce`, fails
closed until regression thresholds are qualified and a separately reviewed
rollout adds them. Whether admission waits for the check is a separate
setting, `BENCH_COMPILER_ADMISSION` (see
[merge-queue admission](../../docs/merge-queue-admission.md#9700x-compiler-comparison-2752)).

The host runs one job at a time because the group holds one runner, so the
compiler comparison and the workload runs never overlap. Each merge group has
its own concurrency group, so no pending comparison replaces another. The
queue's build limit bounds how many are pending at once.

## Administrator steps

None of these can be performed by a pull request.

1. Runner group. `buster-zen5-9700x` is an organization runner in `buster14a`,
   alone in the group `buster-9700x-service-dispatch` (the name predates the
   service's removal). Repository access: selected, only `buster14a/buster`,
   public repositories allowed. Workflow access: selected workflows, exactly
   `buster14a/buster/.github/workflows/9700x-direct-bench.yml@refs/heads/main`.
   Remove the former `9700x-service-dispatch.yml` entry.
2. Create `BENCH_DIRECT_ENABLED` with value `false`; set it to `true` only
   after the host step. Set it back to `false` on any drift.
3. On the host, the runner account needs `clang` with a static C library,
   `python3` 3.9 or newer, `git`, CPU 2 in its allowed set and a writable work
   directory. It needs no sudo rule. The compiler comparison additionally needs
   `tcc`, `cmake`, `ninja`, `perf` usable by that account (a
   `kernel.perf_event_paranoid` that permits user-space counting), `taskset`,
   and anonymous HTTPS access to github.com for the queue-ref read.
4. Create `BENCH_COMPILER_ENABLED` with value `false`. Set it to `true` only
   after a pilot comparison on the host meets the 90-minute job bound. Leave
   `BENCH_COMPILER_REGRESSION_POLICY` unset.
5. Confirm that workflows from fork pull requests still require approval.

Leftovers of the removed service that only an administrator can delete: the
`benchmark-9700x` environment, the Actions requester policy that named
`9700x-service-dispatch.yml`, and the variable `BENCH_SERVICE_DISPATCH_ENABLED`.
