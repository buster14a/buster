# Reviewed Forgejo action references

All `uses` values in `.forgejo/workflows/` name an explicit HTTPS origin and
a reviewed, full lowercase commit SHA. Mutable branches/tags and arbitrary
new revisions fail `.forgejo/scripts/check_action_pins.py`. Local actions and
container action references require a separate policy decision before use.

The checker runs before the canonical gate, each matrix job and privacy-broker
dispatch. GitHub workflow lint runs the same policy and its offline regression
suite, so the policy remains checked during the GitHub migration. Checkout
necessarily precedes repository-local checks; the literal pin at that trust
boundary must itself be reviewed in the PR. This policy cannot prevent a PR
author from changing the checker together with a workflow.

## Current approval

`https://data.forgejo.org/actions/checkout@11d5960a326750d5838078e36cf38b85af677262`
was already used by the privacy broker. On 2026-09-09, the upstream `v4` tag
resolved to that exact commit (`backport fixes to releases-v4 (#2524)`). The
repository was fetched from the named origin and its commit, `action.yml`,
package metadata and checkout/authentication source paths were inspected.
The action uses Node 20, accepts the server URL from the environment, and
retains the `persist-credentials` input. All three invocations explicitly set
that input to false. This pins the version in current use, without changing
checkout inputs, job conditions, matrices or broker credentials.

Source: [reviewed action revision](https://data.forgejo.org/actions/checkout/src/commit/11d5960a326750d5838078e36cf38b85af677262).
The source review is scoped to checkout compatibility and the existing trust
boundary; it is not a comprehensive audit of the bundled action dependencies.
Native Forgejo runner execution is reported separately from offline checks.

## Updating an action

1. Resolve the desired tag at its existing origin. Fetch the full commit and
   inspect the source and bundled entry point changes from the current pin,
   including runtime requirements, authentication handling and post-job cleanup.
2. Update the workflow literals and `APPROVED` in the checker together. Record
   the origin, full commit, tag/date and relevant compatibility changes here.
3. Run `python3 .forgejo/scripts/check_action_pins.py` and
   `python3 tests/action_pins_test.py`. The checker must reject the old mutable
   reference and any unreviewed SHA. Run the existing workflow/helper checks.
4. Validate the exact submitted revision on the affected native Forgejo matrix
   and, when enabled, the privacy broker before claiming those paths pass.

## Deliberately restricted workflow syntax

The checker uses only Python's standard library. It scans block-style mappings,
plain mapping keys and single-line action values, optionally quoted or followed
by a comment. Flow sequences are limited to simple lists of plain scalars
such as branch names or runner labels. It rejects flow mappings, YAML tags,
explicit/quoted keys, mapping anchors/aliases/merges and multiline or expression-based action values. It
skips literal/folded script blocks. It is not a general YAML parser; adding a
different syntax requires a reviewed scanner change and regression. General
GitHub workflow validation continues to use actionlint independently.
