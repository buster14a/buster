# Protected GitHub admission for the Ryzen 7 9700X

The benchmark host is not a general Actions executor. Exactly two workflows
may reach it. `.github/workflows/9700x-service-dispatch.yml` uses the installed
fixed gateway and checks out nothing. `.github/workflows/9700x-direct-bench.yml`
(#2704) compiles and runs the owner's own pull-request workloads directly; see
[Direct workload gate](#direct-workload-gate). No other workflow may check out
code onto this host. The repository variable
`BENCH_SERVICE_DISPATCH_ENABLED` stays `false` until live host qualification.
Only dispatches by `davidgmbb` (user 39247043) reach the runner, and they run
without a manual approval step; the workflow gate in section 2 enforces this.

## 1. Restrict the runner before accepting dispatch requests

The repository is public. Register `buster-zen5-9700x` as an **organization**
runner in `buster14a`, not a repository runner, and move it into the new
`buster-9700x-service-dispatch` runner group. Its configuration must be:

- Repository access: Selected repositories, only `buster14a/buster`. Allow
  public repositories, because this one is public.
- Workflow access: Selected workflows, exactly
  `buster14a/buster/.github/workflows/9700x-service-dispatch.yml@refs/heads/main`
  and `buster14a/buster/.github/workflows/9700x-direct-bench.yml@refs/heads/main`.
- Exactly one runner with labels `self-hosted`, `Linux`, `X64`,
  `buster-zen5`, `ryzen-9700x` in the group. Remove the old repository runner
  registration. Do not attach another runner to the group.

Use GitHub's organization Settings → Actions → Runner groups and Runners. The
runner's host registration requires host administrator access and a fresh
organization registration token; keep tokens off this repository. The workflow
selects both the group and labels. GitHub's group restrictions protect the
public repository even if another workflow later copies the labels.

## 2. Read back repository admission

From a trusted checkout of protected `main`, with access to read repository
policies, collaborator permission and organization runner groups, first read
back `BENCH_SERVICE_DISPATCH_ENABLED=false`. Create it explicitly with value
`false` if it is absent. Do not run the preflight during an enabled dispatch
window. With dispatch staged disabled, run the read-only preflight:

```sh
bash tools/bench_service/deploy/configure_github_admission.sh buster14a/buster
```

The preflight requires the repository variable to be exactly `false` without
changing it, then verifies the exact main merge-queue ruleset `22537199`, the
absence of the superseded benchmark branch ruleset, the runner group, and the
absence of a repository-scoped benchmark runner. It compares live `main`'s
dispatch workflow byte for byte with the trusted checkout and runs the static
workflow policy test, because that workflow's gate is the actor restriction.
It reads and verifies the **existing** repository Actions policy for the exact
workflow, requester list, and manual event; it does not create or replace that
policy.
The existing Actions policy targets only the fixed workflow through
`workflow_dispatch`. The reviewed requester list is
`Repository admin` (role 5), `davidgmbb` (user 39247043), ChatGPT Codex
Connector (installation 158946652, app 1144995), Claude (installation
159756060, app 1236702), and Devin.ai Integration (installation 161964061,
app 811515). GitHub's live Actions policy identifies the three integrations
by installation ID and `IntegrationInstallation` type; the reviewed fixture
pins those exact installed actors. Read back `orgs/buster14a/installations`
to verify each installation-to-app mapping. Reinstallation changes this
identity and requires a new policy review. These actors can start the workflow,
but only `davidgmbb` passes its gate; any other requester's run is skipped and
never reaches the self-hosted runner. Do not add another requester. The
existing main merge queue keeps all eight checks, including `CI complete` and
`Benchmark service workflow policy`, 20 concurrent builds, one merge and
`ALLGREEN`. Its two reviewed
standing bypass actors are Repository admin (role 5) and `davidgmbb` (user
39247043), both in `always` mode. Audit the exact list; their bypass can skip
normal main protection. This is administrator authority under the reviewed
contract, not a workflow admission result or a connector permission.

The preflight verifies that `davidgmbb` still has repository `admin`
permission. The `benchmark-9700x` environment has **no required reviewer, no
wait timer and no manual approval step**; the verifier fails if a
`required_reviewers`, `wait_timer` or any rule other than the deployment branch
policy is present. It keeps exactly one deployment branch, `main`, with custom
branch policies and without protected-branch mode. If any readback fails,
leave dispatch disabled and investigate before retrying. The preflight fetches
the environment, deployment branch policies and repository variable again. It
checks the absence of reviewer and timer rules, the one exact `main` branch,
and literal `false` dispatch state. It separately rereads the requester policy
and administrator permission. Keep those responses, the organization
installation mapping and the preflight log as administrator receipts; they do
not replace the physical host checks.

### Dispatch gate

The workflow, not an environment approval, restricts execution to `davidgmbb`.
Its first job, `authorize`, runs on a GitHub-hosted `ubuntu-24.04` runner with
only `actions: read`, performs no checkout, and reads this exact run attempt
from `GET /repos/{repository}/actions/runs/{run_id}/attempts/{run_attempt}`.
It fails closed unless the event is `workflow_dispatch`, the head branch is
`main`, both `actor` and `triggering_actor` are login `davidgmbb` **and**
numeric ID 39247043, and the returned attempt equals the running attempt. Only
then does it output `attempt=<run_attempt>`. Context values reach its script
through `env:`; no expression is interpolated into shell source.

The self-hosted `submit` job needs `authorize` and requires `main`,
`BENCH_SERVICE_DISPATCH_ENABLED == 'true'`, `workflow_dispatch`, actor
`davidgmbb` with actor ID 39247043, triggering actor `davidgmbb`, and an
`authorize` output equal to the current `github.run_attempt`. `authorize`
carries the same context checks, so other requesters' runs skip both jobs
without an API call. GitHub reuses the outputs of jobs that succeeded in an
earlier attempt when failed or single jobs are re-run, so the attempt binding
prevents anyone's re-run from inheriting an old authorization; `submit` runs
only after `authorize` itself passes in the same attempt. A maintainer re-run
must use **Re-run all jobs**. `submit` keeps `environment: benchmark-9700x` so
the main-only deployment branch rule still applies.

The actor restriction governs **who starts the workflow**, not who edits its
definition or the installed gateway. Admins must control changes to the
workflow, policy files, installed binary, recipe and host registration.
Changes to any of these need review under the repository's trust policy before
live activation; do not equate a passing static policy check with approval of
arbitrary new commands on a self-hosted runner.

### Direct workload gate

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
fails before the self-hosted runner. An agent that pushes with the owner's
credentials is the owner for this gate.

`bench` receives no token capability, no secret and no environment. It checks
out `main`'s `tools/bench_direct` as the trusted harness and the pull request
head's `benchmarks/9700x` as data, both without persisted credentials, then
runs only `trusted/tools/bench_direct/run_workloads.py`. The pull request
supplies C source that is compiled and executed as the runner account. That is
the intended capability, and it is why the author gate is the whole control:
this path has none of the service's containment, lease or sealed results. Do
not widen the author list.

Administrator steps, none of which a pull request can perform:

1. Add the second selected workflow to the runner group as in section 1.
2. Create `BENCH_DIRECT_ENABLED` with value `false`; set it to `true` only
   after the host step below. Set it back to `false` on any drift.
3. On the host, give the runner account `clang`, `python3`, `git` and a work
   directory it can write. Its sudo rule stays limited to the fixed gateway.
4. Confirm that workflows from fork pull requests still require approval.

The read-only preflight in section 2 verifies the two-workflow runner group
and compares both workflows on `main` byte for byte with the trusted checkout.

### Settings transition

The repository change does not alter settings. Apply the transition from the
former required-reviewer contract in two separate steps:

1. After this workflow gate is on protected `main`, the administrator removes
   the required reviewer from `benchmark-9700x`, keeps its one `main`
   deployment branch rule, adds no wait timer, and leaves
   `BENCH_SERVICE_DISPATCH_ENABLED=false`. With the variable still `false`,
   run the read-only preflight above and retain its receipts.
2. Only after the host operator posts readiness matching the exact
   installation packet (section 3 and
   [`ISSUE_880_OPERATOR_PACKET.md`](ISSUE_880_OPERATOR_PACKET.md)), the
   administrator sets `BENCH_SERVICE_DISPATCH_ENABLED=true` as in section 4.

## 3. Verify the host boundary before activation

Record exact reviewed source and installed binary hashes, host/boot identity,
fixed recipe/profile identity, runner and service UID/GID, stable lease
inode/device, queue/source/result root ownership, sudo/system-bus unit
authorization, cgroup limits, and verifier output after a fresh boot. Prove the
runner principal can invoke only the fixed gateway, cannot mutate queue, lease
or source storage, run arbitrary service commands, or become the candidate or
service identity. Confirm that no other GitHub workflow, Forgejo workflow,
timer, cron, shell process or retained checkout can execute on this host.

Run `python3 -B tools/bench_service/workflow_policy_test.py` on protected
`main` and inspect the Actions policy, group, environment branch restriction,
runner registration, and `BENCH_SERVICE_DISPATCH_ENABLED=false` live. Confirm
the service queue has no unfinished job or unresolved reconciliation. Record
the results in #880. This check requires direct host access; repository settings
alone do not prove the host is safe.

## 4. Activate and dispatch

After the host checks pass, the host operator has posted readiness matching
the exact installation packet, and evidence is retained, the administrator
enables the repository variable once (transition step 2):

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body true --repo buster14a/buster
```

`davidgmbb` dispatches the fixed workflow from protected `main` with full
lowercase immutable commit IDs, a stable idempotency key and a `recipe` from
the reviewed allowlist (default `validate-buster-v1`); the job runs
without an approval step once `authorize` passes. Runs started by any other
permitted requester, or re-run by anyone other than `davidgmbb`, are skipped
before the self-hosted runner. The service
owns idle-only atomic admission and cleanup; Actions concurrency is only a UI
guard. Leave the variable enabled during normal verified operation. Disable
it immediately on policy, workflow, host, or verifier drift, or on ambiguous
service recovery:

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body false --repo buster14a/buster
```

After changing the workflow or its gate, main ruleset, Actions policy,
environment, preflight, installed gateway, recipe, or profile, verify the
reviewed admission and host again before re-enabling dispatch. Re-adding a
required reviewer or wait timer is drift from this contract: disable dispatch
and review the change.
