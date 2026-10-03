# Approved GitHub action references

`tools/check_action_pins.py` checks every `.yml` and `.yaml` workflow under
`.github/workflows/` and every local action manifest under `.github/actions/`.
Each remote `uses` value must name an approved GitHub action path and a full
lowercase commit SHA. Mutable branches/tags, unlisted paths and unapproved
revisions fail. Local and container references require a separate policy
decision before use; approved same-commit references are listed below.

The `Workflow lint` job runs the checker and `tests/action_pins_test.py` before
actionlint. The checker now lives under `tools/` because the Forgejo workflows
and their script directory were removed. Its allowlist preserves existing revisions and records staged migrations; an
older pin remains approved only while at least one reviewed workflow still uses it.

| Action path | Approved commit | Existing release annotation |
|---|---|---|
| `actions/checkout` | `11bd71901bbe5b1630ceea73d27597364c9af683` | v4.2.2 |
| `actions/upload-artifact` | `ea165f8d65b6e75b540449e92b4886f43607fa02` | v4.6.2 (legacy workflows; Node 20) |
| `actions/upload-artifact` | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` | v7.0.1 (Buster CI; Node 24) |
| `actions/cache/restore` | `0057852bfaa89a56745cba8c7296529d2fc39830` | v4.3.0 |
| `actions/cache/save` | `0057852bfaa89a56745cba8c7296529d2fc39830` | v4.3.0 |

The Buster CI workflow moved to the immutable v7.0.1 commit after run
35167957822 completed its required Windows x86-64 Release work but the final
v4.6.2 artifact step, which GitHub forced from Node 20 onto Node 24, failed
while creating the artifact. The v7 action declares Node 24 natively and keeps
the existing artifact names, paths, retention, compression and failure policy.
Other workflows remain on v4.6.2 until their independent validation.

Checkout necessarily precedes repository-local checks; its literal pin must
itself be reviewed in the PR. This policy cannot prevent a PR author from
changing the checker together with a workflow.

## Approved local CI evidence-upload composite action

`./.github/actions/native-artifact-upload` is approved only for two Buster CI
uses: the packed native evidence step after successful packaging, and the
mobile log-retention step after `Mobile result and reproduction`. The mobile
caller runs it under `!cancelled()`, so build, lifecycle, test, and coverage
failures still retain the available logs. Its inputs preserve the mobile
artifact name and path, the upload action's default compression level (6),
`if-no-files-found: ignore`, and seven-day retention.

The composite uses the existing pinned upload-artifact v7 action for the
initial attempt and one retry, tolerates only the initial failure, waits
15 seconds, and sets `overwrite: true` on the blocking retry. Recovery and
evidence loss are reported explicitly. A successful retry does not change any
earlier build or coverage failure. Regression coverage in
`tools/ci_native_observation_test.py` checks both callers, all three mobile
matrix entries, the matching retry inputs, cancellation, and the two-attempt
limit.

The checker allows this exact local path, then scans its manifest with the same
remote action allowlist. It does not authorize arbitrary local or container
actions.

## Approved same-commit reusable workflow

`./.github/workflows/throughput-real-source.yml` reuses the existing native
workload qualification after both pinned apt profiles pass, without copying its build/admission commands into a
second harness. GitHub resolves this literal `./` workflow from the caller's
same commit; there is no floating external branch, tag or downloaded action.
The called workflow retains read-only contents permission and its existing
source identity, native oracle, admission and artifact checks. Direct PR runs
remain opt-in; only the path-filtered apt qualification sets the new boolean
input. See [pinned input qualification](ci-apt-inputs.md).

`./.github/workflows/compiler-throughput.yml` is also approved for the
same-commit `Compiler throughput requests` caller. It shares the reviewed PR
measurement jobs while the caller owns the bounded manual/schedule queue.
The called workflow uses a run-unique group for non-PR requests, preventing
another default single-pending group from replacing a waiting request. It
retains read-only contents permission and no inherited secrets. See
[`tools/throughput/README.md`](../tools/throughput/README.md#hosted-workflow-requests).

The checker does not accept arbitrary local actions, path traversal, local
`@ref` suffixes, expressions or unreviewed remote references. The additional
controls in `tests/ci_apt_test.py` exercise those rejection boundaries; the
existing action-policy tests and independent actionlint remain required.

The pinned actionlint predates GitHub's `concurrency.queue` syntax. Workflow
lint ignores only its unexpected-`queue`-key diagnostic; the offline policy
test confines `queue: max` to the reviewed request workflow and checks that it
is paired with `cancel-in-progress: false`. All other actionlint diagnostics
still fail CI.

## Updating an action

1. Resolve the desired tag at its existing origin. Fetch the full commit and
   inspect source and bundled entry point changes from the current pin,
   including runtime requirements, authentication handling and post-job cleanup.
2. Update workflow literals and `APPROVED` in the checker together. Record
   the action path, full commit, tag/date and compatibility changes here.
3. Run `python3 tools/check_action_pins.py`, `python3 tests/action_pins_test.py`
   and `go run github.com/rhysd/actionlint/cmd/actionlint@03d0035246f3e81f36aed592ffb4bebf33a03106 .github/workflows/*.yml`.
   Mutable references and unapproved SHAs must still fail.
4. Validate the submitted revision on the affected GitHub jobs before claiming
   those paths pass.

## Deliberately restricted workflow syntax

The checker uses only Python's standard library. It scans block-style mappings,
plain mapping keys and single-line action values, optionally quoted or followed
by a comment. It supports simple scalar lists, quoted path-list entries and
empty mappings such as `permissions: {}`. GitHub expressions are treated as
scalar content for structural checks, but cannot select an action reference.

Nonempty flow mappings, nested flow collections, YAML tags, explicit/quoted
mapping keys, mapping anchors/aliases/merges and multiline action references
are rejected. Literal/folded script blocks are skipped. It is not a general
YAML parser; adding syntax requires a reviewed scanner change and regression.
General GitHub workflow validation continues to use actionlint independently.

## GitHub Pages actions

Added for the static-site integration (#2436), with immutable upstream tag
resolution, manifests, source entry points and license files inspected on
2026-10-03. Existing action permissions and revisions are unchanged.

| Action path | Approved commit | Version / runtime | License source |
|---|---|---|---|
| `actions/upload-pages-artifact` | `fc324d3547104276b827a68afc52ff2a11cc49c9` | v5.0.0; composite, Node 24 upload | [MIT](https://github.com/actions/upload-pages-artifact/blob/fc324d3547104276b827a68afc52ff2a11cc49c9/LICENSE) |
| `actions/deploy-pages` | `368f82528645a54fb793d4d04e342629a3f51346` | v5.0.1; Node 24 | [MIT](https://github.com/actions/deploy-pages/blob/368f82528645a54fb793d4d04e342629a3f51346/LICENSE) |

The upload action's [pinned manifest](https://github.com/actions/upload-pages-artifact/blob/fc324d3547104276b827a68afc52ff2a11cc49c9/action.yml)
creates `artifact.tar` from its selected directory, dereferences links, excludes
hidden files by default, and fails if the archive is missing. Buster validates
its exact two-file `site/` payload and rejects symlinks/hardlinks before calling
it. Its transitive upload is already immutable:
`actions/upload-artifact@bbbca2ddaa5d8feaa63e36b76fdaad77386f024f` (v7.0.0;
[Node 24 manifest](https://github.com/actions/upload-artifact/blob/bbbca2ddaa5d8feaa63e36b76fdaad77386f024f/action.yml),
[MIT license](https://github.com/actions/upload-artifact/blob/bbbca2ddaa5d8feaa63e36b76fdaad77386f024f/LICENSE)).
That nested revision is not an additional approved direct reference in Buster;
existing direct upload-artifact pins stay unchanged. Upload retention is one day.

The deploy action's [manifest](https://github.com/actions/deploy-pages/blob/368f82528645a54fb793d4d04e342629a3f51346/action.yml)
selects `dist/index.js`. Its [source entry point](https://github.com/actions/deploy-pages/blob/368f82528645a54fb793d4d04e342629a3f51346/src/index.js)
requests an OIDC token; the [API client](https://github.com/actions/deploy-pages/blob/368f82528645a54fb793d4d04e342629a3f51346/src/internal/api-client.js)
requires exactly one matching artifact from the current workflow run and sends
its ID, build version and OIDC token to the Pages deployment API. The
[deployment controller](https://github.com/actions/deploy-pages/blob/368f82528645a54fb793d4d04e342629a3f51346/src/internal/deployment.js)
polls for success with capped backoff/jitter, reports errors, and attempts
cancellation on timeout or a workflow cancellation signal. There is no post-job
checkout cleanup or arbitrary build command in Buster's privileged job. Its
only permissions are `pages: write` and `id-token: write`; site activation is an
administrator prerequisite, not an action-side privilege escalation.

This is a source/manifest provenance review, not an independent reproducible-build
attestation of the bundled JavaScript dependency graph. These MIT licenses cover
the named upstream action projects, not all transitive packages or Buster's
first-party code. No upstream source, theme, or license text is copied into the
published site. See [Pages setup and acceptance](github-pages.md).
