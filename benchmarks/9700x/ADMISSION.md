# Admission for the direct 9700X workload workflow

The Ryzen 7 9700X is not a general Actions executor. Exactly one workflow may
reach it: `.github/workflows/9700x-direct-bench.yml` (#2704), which compiles
and runs the owner's own pull-request workloads. Its hosted marker,
`.github/workflows/9700x-direct-request.yml`, never selects the runner. The queued benchmark service
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

The actor restriction governs who starts the workflow, not who edits its
definition. Changes to the workflow, its harness or this policy test need
owner review before they reach `main`.

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
   directory. It needs no sudo rule.
4. Confirm that workflows from fork pull requests still require approval.

Leftovers of the removed service that only an administrator can delete: the
`benchmark-9700x` environment, the Actions requester policy that named
`9700x-service-dispatch.yml`, and the variable `BENCH_SERVICE_DISPATCH_ENABLED`.
