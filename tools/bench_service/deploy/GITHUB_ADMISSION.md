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

## 2. Install and read back repository admission

From a trusted checkout of protected `main`, with repository Administration
write access and organization runner-group read access, first read back
`BENCH_SERVICE_DISPATCH_ENABLED=false`. Create it explicitly with value
`false` if it is absent. Do not rerun the installer during an enabled dispatch
window. With dispatch staged disabled, run:

```sh
bash tools/bench_service/deploy/configure_github_admission.sh buster14a/buster
```

The installer requires the repository variable to be exactly `false` without
changing it, then verifies the exact main
merge-queue ruleset `22537199`, verifies the runner group and rejects a
repository-scoped benchmark runner. It reads and verifies the **existing**
repository Actions policy for the exact workflow, requester list, and manual
event; it never creates or replaces that policy. It then installs the additional
benchmark branch ruleset. The existing Actions policy targets only the fixed
workflow through `workflow_dispatch`. The reviewed requester list is
`Repository admin` (role 5), `davidgmbb` (user 39247043), ChatGPT Codex
Connector (app 1144995), Claude (app 1236702), and Devin.ai Integration
(app 811515). These actors can start the workflow, but they do not thereby
release its protected environment job. Do not add another requester. The
additional branch ruleset retains deletion/force-push protection and
`CI complete` plus `Benchmark service workflow policy`, without imposing
one approval on every PR. The original main merge queue keeps all eight
checks, 20 concurrent builds, one merge and `ALLGREEN`. Its two reviewed
standing bypass actors are Repository admin (role 5) and `davidgmbb` (user
39247043), both in `always` mode. Audit the exact list; a bypass can skip
normal main protection and must not be mistaken for an admission result.

The installer verifies that `davidgmbb` still has repository `admin`
permission, then configures that user as the sole required reviewer for
`benchmark-9700x` with self-review prevention. The environment remains
restricted to the one exact `main` deployment branch. Connector requests
wait for that administrator's approval before the runner is assigned a job;
an administrator starting a run cannot approve their own request. If any
readback fails, leave dispatch disabled and investigate before retrying.
GitHub currently allows repository administrators to bypass environment
protection; using that control is also an explicit administrator release and
must be recorded as such. It does not count as the required review receipt.
After installation, the read-only verifier fetches the benchmark ruleset,
environment, deployment branch policies and repository variable again. It
checks the reviewed ruleset without bypass actors, the administrator reviewer
and self-review prevention, the one exact `main` branch, and literal `false`
dispatch state. The installer separately rereads the requester policy and
administrator permission after configuration.
Keep those responses and the installer log as administrator receipts; they do
not replace the physical host checks.

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

After changing the workflow, ruleset payload, Actions policy, installer,
installed gateway, recipe, or profile, verify the reviewed installation and
host again before re-enabling dispatch.
