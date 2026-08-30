# GitHub-hosted runner privacy bridge

Buster can add GitHub-hosted Linux, macOS, and Windows capacity to Forgejo 16
without mirroring the repository, uploading Actions artifacts, or leaving any
durable source-granting credential at GitHub. Forgejo stays the source of
truth and owns the final status. A private, workflow-only GitHub repository is
only a broker:

```text
trusted Forgejo main SHA
  -> generate ephemeral read-only deploy key (in memory / /dev/shm)
  -> register the public half on Forgejo, seal the private half into a
     GitHub repository secret (libsodium sealed box)
  -> workflow_dispatch(SHA, random nonce)
  -> fresh hosted VMs fetch that exact SHA over SSH straight into a
     memory-backed filesystem, build and test there, destroy it
  -> Forgejo polls the GitHub run, records success or failure, then
     deletes the GitHub secret and revokes the deploy key
```

## Threat model, honestly stated

The design minimizes what GitHub *retains* and what a *breach* of GitHub can
yield. It cannot make GitHub technically unable to read the source while a
job runs: a hosted VM must hold plaintext in memory to compile it, and the
platform controls that VM and its hypervisor. Only runners on hardware you
control, or attested confidential-computing VMs with externally released
keys, remove the provider from the trust boundary. With that stated:

- **Retention.** GitHub durably stores the broker workflow file, ordinary
  run metadata (nonce, commit ID, runner names, exit codes, timings), and
  two non-secret variables (clone URL, pinned host key). No source, no
  binaries, no build logs, no caches, no artifacts, no mirror.
- **Security breach.** Between runs, GitHub's secret store holds nothing:
  the per-run deploy key is deleted after every run and revoked on Forgejo
  regardless, so even a copy retained by GitHub's infrastructure stops
  working minutes after it was created. There is no long-lived credential
  at GitHub whose theft would grant repository access. The commit ID in the
  retained metadata is not secret.
- **Training / scanning.** There is no repository content at rest on
  GitHub's side to ingest, index, or scan. What exists is plaintext in RAM
  on an ephemeral VM for the duration of one job.
- **Tracking / inspection.** The retained job log carries fixed status
  lines and exit codes only; build output goes to a file on the memory
  filesystem and dies with it. Core dumps are disabled, shell tracing is
  never enabled, and the key reaches ssh-agent through a pipe (never a
  temporary file, never argv, never `$GITHUB_ENV`).

## Where the plaintext can exist, per platform

| Platform | Source, build tree, temp files, logs | Persistent-media exposure |
|---|---|---|
| Linux | tmpfs mounted `mode=0700` after `swapoff -a` (plus `noswap` where supported) | None: no page of the workspace or of any build process can be swapped out |
| macOS | HFS+ RAM disk (Spotlight indexing off) | RAM-disk pages can reach VM swap under memory pressure; macOS encrypts swap by default |
| Windows | Plain directory on the ephemeral VM disk, deleted afterwards | Plaintext touches NTFS until deletion; no secure-erase promise from the cloud provider |

Encryption sits at every transfer boundary: the fetch is SSH with a pinned
host key, the key delivery is a libsodium sealed box to GitHub's secret
store, and the Forgejo side generates the keypair under `/dev/shm` and
zero-overwrites the files before unlinking. There is deliberately no
"encrypted source bundle" step: any key that GitHub-hosted compute can use
to decrypt is a key GitHub can observe, so a bundle would add moving parts
without adding a boundary. The RAM-only workspace is the part of the
"decrypt into memory" idea that actually holds on hosted hardware.

## One-time GitHub broker setup

1. Create a new **private** GitHub repository. Do not mirror Buster into it
   and do not enable Pages, releases, packages, caches, or artifact uploads.
2. Copy `.forgejo/github-bridge/forgejo-hosted.yml` from Buster to
   `.github/workflows/forgejo-hosted.yml` in that repository. The workflow
   uses no checkout action or third-party action and declares
   `permissions: {}`. It must exist on the broker's default branch for the
   `workflow_dispatch` API.
3. Add these GitHub Actions repository **variables** (they are not secrets):

   - `FORGEJO_CLONE_URL`: the SSH clone URL for Buster;
   - `FORGEJO_KNOWN_HOSTS`: a pinned Forgejo SSH host-key line verified
     against the server administrator's fingerprint. Do not trust an
     unverified `ssh-keyscan` result.

   Do **not** create a `FORGEJO_DEPLOY_KEY` secret by hand: the dispatcher
   creates and deletes it around every run.
4. Create a fine-grained personal access token (or GitHub App installation
   token source) scoped to this broker repository only, with repository
   permissions `Actions: write`, `Secrets: write`, and `Metadata: read`.
   Store it only as the Forgejo Actions secret `GH_BRIDGE_TOKEN`.

The default runner labels are `ubuntu-24.04`, `macos-15`, and
`windows-2022`; change the template deliberately if the GitHub account
requires different hosted labels.

## Forgejo 16 configuration

The dispatcher runs on a trusted Forgejo runner and needs `python3`,
`ssh-keygen`, and one of **PyNaCl** (`pip install pynacl`) or the **gh**
CLI to seal the per-run secret. Create these repository Actions variables:

| Variable | Value |
|---|---|
| `GH_BRIDGE_REPOSITORY` | GitHub `OWNER/REPOSITORY` for the broker |
| `GH_BRIDGE_WORKFLOW` | `forgejo-hosted.yml` (default if unset) |
| `GH_BRIDGE_REF` | Broker default branch, normally `main` (default if unset) |
| `GH_BRIDGE_FORGEJO_REPOSITORY` | `buster/buster` (default if unset) |
| `GH_BRIDGE_FORGEJO_API_URL` | `https://code.buster14a.com/api/v1` (default if unset) |
| `GH_HOSTED_RUNNERS_ENABLED` | `true` only after all setup is complete |

And these repository Actions secrets:

| Secret | Value |
|---|---|
| `GH_BRIDGE_TOKEN` | The fine-grained GitHub token described above |
| `GH_BRIDGE_FORGEJO_TOKEN` | A Forgejo token that can manage this repository's deploy keys |

The `.forgejo/workflows/github-hosted.yml` workflow runs only for pushes to
`main`. It deliberately has no manual or pull-request trigger because a
selected branch can carry a modified, secret-stealing workflow. It is
separate from the existing native matrix and exact-SHA main gate, so leaving
`GH_HOSTED_RUNNERS_ENABLED` unset keeps current CI unchanged. After a
successful trial, make the bridge job's exact status context required in the
applicable Forgejo branch rule if it should gate merges. The job also checks
`github.ref == 'refs/heads/main'` as defense in depth.

## Per-run credential lifecycle

1. The dispatcher revokes any `github-bridge-*` deploy key older than two
   hours (leftovers of a crashed run), then waits until no broker run is
   active, because the single secret name serializes runs.
2. It generates a fresh ed25519 keypair under `/dev/shm`, registers the
   public half as a **read-only** Forgejo deploy key titled
   `github-bridge-<nonce>`, and seals the private half into the broker
   secret `FORGEJO_DEPLOY_KEY`.
3. It dispatches the broker workflow with only the nonce and the exact
   commit ID, discovers the run by its `forgejo-<nonce>` run name (the
   dispatch API returns no run ID), and polls it to completion, requesting
   cancellation on timeout or interruption.
4. Whatever happened, it deletes the GitHub secret and revokes the Forgejo
   deploy key. Either half alone is useless; both deletions failing at once
   still leaves a read-only, single-repository key that the next dispatch's
   stale-key sweep revokes.

Each hosted platform compiles the build driver with the Clang already
present on the hosted image, configures the Clang CI tree, and runs the
Release `test_all` target inside the memory workspace. This narrow
bootstrap exception avoids downloading an unpinned compiler before the
source fetch and avoids modern macOS's lack of a working TCC package. It is
supplementary portable coverage, not a replacement for
`test_all_combinations_ci`: the controlled Forgejo matrix retains the exact
Clang/GCC/Zig versions, sanitizers, fuzzing, self-hosting, Android, and iOS
coverage required by this repository.

## Trust rules

- Keep automatic dispatch restricted to protected branches. Never add a
  secret-bearing bridge job to a fork pull-request trigger.
- Require review for changes to `.forgejo/workflows/github-hosted.yml`,
  `.forgejo/scripts/github_runner_bridge.py`, and the installed broker
  workflow. A trusted main commit can deliberately exfiltrate its own
  source; this architecture cannot prevent that.
- Keep the Forgejo checkout action pinned to its reviewed commit rather
  than a mutable major-version tag, because it prepares the dispatcher that
  receives both API tokens in the next step.
- Scope `GH_BRIDGE_FORGEJO_TOKEN` to deploy-key management on this one
  repository if the Forgejo token model allows it, and rotate it like any
  administrative credential. It never leaves the Forgejo runner.
- Do not add `actions/cache`, `upload-artifact`, verbose failure-log
  printing, source mirroring, or package-manager toolchain installation to
  the broker. Those features create persistent copies or introduce code
  that can run before or beside the credentialed fetch.
- Do not weaken the memory-workspace step: everything derived from the
  source (objects, binaries, fixture output, logs, `TMPDIR`) must stay
  under the RAM root on Linux and macOS.
- If the Forgejo server refuses fetches of unadvertised objects, set
  `uploadpack.allowAnySHA1InWant` server-side rather than teaching the
  broker to fetch a branch: the broker must only ever fetch the exact
  commit it was told.
- Mobile tests remain on the existing controlled Forgejo runners. The
  current Android and iOS workflows depend on KVM/emulator and simulator
  behavior that this desktop broker does not claim to reproduce.

## Local verification

The validation, sealing, polling, cancellation, and credential-lifecycle
behavior is covered without contacting GitHub or Forgejo:

```sh
python3 tests/github_runner_bridge_test.py
python3 -m py_compile .forgejo/scripts/github_runner_bridge.py
actionlint .forgejo/github-bridge/forgejo-hosted.yml
```

For the first live run, enable the variables and push a reviewed change to
`main`, confirm that the GitHub broker run name begins with `forgejo-`, and
verify afterwards that the broker repository has no Actions artifacts or
caches, that its `FORGEJO_DEPLOY_KEY` secret is gone, and that the Buster
repository lists no `github-bridge-*` deploy keys.

## Platform references

- [Forgejo 16 Actions and GitHub Actions compatibility](https://forgejo.org/docs/v16.0/user/actions/github-actions/)
- [Forgejo 16 Actions administration and runner model](https://forgejo.org/docs/v16.0/admin/actions/)
- [Forgejo Actions security](https://forgejo.org/docs/v16.0/user/actions/security/)
- [GitHub workflow-dispatch REST API](https://docs.github.com/en/rest/actions/workflows#create-a-workflow-dispatch-event)
- [GitHub Actions secrets REST API](https://docs.github.com/en/rest/actions/secrets)
- [GitHub-hosted runners](https://docs.github.com/en/actions/using-github-hosted-runners/about-github-hosted-runners)
- [GitHub Actions security hardening](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions)
