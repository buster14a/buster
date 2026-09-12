# Approved GitHub action references

`tools/check_action_pins.py` checks every `.yml` and `.yaml` workflow under
`.github/workflows/`. Each `uses` value must name an approved GitHub action
path and a full lowercase commit SHA. Mutable branches/tags, unlisted paths
and unapproved revisions fail. Local and container actions require a separate
policy decision before use.

The `Workflow lint` job runs the checker and `tests/action_pins_test.py` before
actionlint. The checker now lives under `tools/` because the Forgejo workflows
and their script directory were removed. Its allowlist preserves the existing
GitHub workflow revisions; this migration does not update any action source.

| Action path | Approved commit | Existing release annotation |
|---|---|---|
| `actions/checkout` | `11bd71901bbe5b1630ceea73d27597364c9af683` | v4.2.2 |
| `actions/upload-artifact` | `ea165f8d65b6e75b540449e92b4886f43607fa02` | v4.6.2 |
| `actions/cache/restore` | `0057852bfaa89a56745cba8c7296529d2fc39830` | v4.3.0 |
| `actions/cache/save` | `0057852bfaa89a56745cba8c7296529d2fc39830` | v4.3.0 |

Checkout necessarily precedes repository-local checks; its literal pin must
itself be reviewed in the PR. This policy cannot prevent a PR author from
changing the checker together with a workflow.

## Updating an action

1. Resolve the desired tag at its existing origin. Fetch the full commit and
   inspect source and bundled entry point changes from the current pin,
   including runtime requirements, authentication handling and post-job cleanup.
2. Update workflow literals and `APPROVED` in the checker together. Record
   the action path, full commit, tag/date and compatibility changes here.
3. Run `python3 tools/check_action_pins.py`, `python3 tests/action_pins_test.py`
   and `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 .github/workflows/*.yml`.
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
