# Protected GitHub admission for the Ryzen 7 9700X

The benchmark host is not a general Actions executor. The sole Actions path
uses the installed fixed gateway in `.github/workflows/9700x-service-dispatch.yml`;
no workflow may check out candidate code onto this host. The repository variable
`BENCH_SERVICE_DISPATCH_ENABLED` stays `false` until live host qualification.

## 1. Restrict the runner before accepting connector requests

The repository is public. Register `buster-zen5-9700x` as an **organization**
runner in `buster14a`, not a repository runner, and move it into the new
`buster-9700x-service-dispatch` runner group. Its configuration must be:

- Repository access: Selected repositories, only `buster14a/buster`. Allow
  public repositories, because this one is public.
- Workflow access: Selected workflows, exactly
  `buster14a/buster/.github/workflows/9700x-service-dispatch.yml@refs/heads/main`.
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
absence of a repository-scoped benchmark runner. It reads
and verifies the **existing** repository Actions policy for the exact workflow,
requester list, and manual event; it does not create or replace that policy.
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
but they do not thereby release its protected environment job. Do not add
another requester. The existing main merge queue keeps all eight checks,
including `CI complete` and
`Benchmark service workflow policy`, 20 concurrent builds, one merge and
`ALLGREEN`. Its two reviewed
standing bypass actors are Repository admin (role 5) and `davidgmbb` (user
39247043), both in `always` mode. Audit the exact list; their bypass can skip
normal main protection. This is administrator authority under the reviewed
contract, not a workflow admission result or a connector permission.

The preflight verifies that `davidgmbb` still has repository `admin`
permission and is the sole required reviewer for
`benchmark-9700x` with self-review prevention. The environment remains
restricted to the one exact `main` deployment branch. Connector requests
wait for that administrator's approval before the runner is assigned a job;
an administrator starting a run cannot approve their own request. If any
readback fails, leave dispatch disabled and investigate before retrying.
GitHub currently allows repository administrators to bypass environment
protection; using that control is also an explicit administrator release and
must be recorded as such. It does not count as the required review receipt.
The preflight fetches the environment, deployment branch policies and
repository variable again. It checks the administrator reviewer and
self-review prevention, the one exact `main` branch, and literal `false`
dispatch state. It separately rereads the requester policy and administrator
permission. Keep those responses, the organization installation mapping and
the preflight log as administrator receipts; they do not replace the physical
host checks.

The actor restriction governs **who starts the workflow**, not who edits its
definition or the installed gateway. Admins must control changes to the
workflow, policy files, installed binary, recipe and host registration.
Changes to any of these need review under the repository's trust policy before
live activation; do not equate a passing static policy check with approval of
arbitrary new commands on a self-hosted runner.

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

The dispatch workflow uses one constant concurrency group with `queue: max`.
GitHub retains at most 100 pending runs in that group; additional runs are
cancelled when it is full. Waiting order follows when runs enter the concurrency
group and does not promise dispatch order. An environment approval wait may
extend a request's lifetime. Operators must inspect each run's actual state and
record cancellations, rejections and supersessions rather than treating a
submitted workflow ID as service admission. Once the fixed gateway reports a
job and attempt, the service's own lease and durable journal control execution
and cleanup even if the Actions client disconnects or is cancelled. The service
itself retains at most eight pending jobs and 64 jobs for its journal lifetime;
its capacity failures require an explicit retained disposition, not a journal
reset. See [GitHub's concurrency reference](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#concurrency).

After the host checks pass and evidence is retained, enable the repository
variable once:

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body true --repo buster14a/buster
```

An approved connector can request the fixed workflow from protected `main`
with full lowercase immutable commit IDs and a stable idempotency key. The
reviewed repository administrator approves or rejects the pending environment
job after checking the request. The service
owns idle-only atomic admission and cleanup; Actions concurrency is only a UI
guard. Leave the variable enabled during normal verified operation. Disable
it immediately on policy, workflow, host, or verifier drift, or on ambiguous
service recovery:

```sh
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body false --repo buster14a/buster
```

After changing the workflow, main ruleset, Actions policy, preflight,
installed gateway, recipe, or profile, verify the reviewed admission and
host again before re-enabling dispatch.
