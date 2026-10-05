# Admission for the direct 9700X workload workflow

The Ryzen 7 9700X is not a general Actions executor. Exactly one workflow may
reach it: `.github/workflows/9700x-direct-bench.yml` (#2704), which compiles
and runs the owner's own pull-request workloads. The queued benchmark service
and its dispatch workflow are removed (#2708). No other workflow may select
the runner, and `tools/bench_direct/workflow_policy_test.py` fails the required
"Benchmark service workflow policy" check if one does. That check keeps its
historical name because the `main` ruleset requires it by name.

## Gate

The workflow starts on `pull_request_target` for changes to
`benchmarks/9700x/*.c`, with no manual dispatch and no approval.
`pull_request_target` runs the workflow definition from `main`, so a pull
request cannot edit the gate and the run matches the runner group's
`@refs/heads/main` pin. A `push` or `pull_request` trigger would run the
branch's own copy of the workflow and must not be used for this host. To run a
workload from a branch, open a pull request from it; a draft is enough.

Both jobs require the repository variable `BENCH_DIRECT_ENABLED == 'true'`,
the pull request author to be `davidgmbb` by login and numeric ID 39247043,
the head repository to be this repository, and `davidgmbb` as actor, actor ID
and triggering actor. The hosted `authorize` job has only `actions: read`,
performs no checkout, and re-reads this exact run attempt from GitHub before
emitting `attempt=<run_attempt>`; `bench` runs only with an authorization from
the same attempt. GitHub reuses the outputs of jobs that succeeded in an
earlier attempt, so a maintainer re-run must use **Re-run all jobs**. A pull
request by any other author, from a fork, or pushed to by another account
skips both jobs before the self-hosted runner. An agent that pushes with the
owner's credentials is the owner for this gate.

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
