# Main admission policy

Settings verified 2026-09-20 UTC against main `b7346b3e` for #556.
The active repository ruleset 22537199 explicitly includes `refs/heads/main`,
excludes nothing, and has no bypass actors. The adjacent JSON files preserve
before/after settings and the effective branch-rules response; they contain no
credentials. Repository rules, rather than legacy branch protection, enforce it.

## Required review and checks

Every PR needs one approval, dismissal of stale approvals after new commits,
approval of the latest push by someone other than its pusher, and resolved
review threads. Code-owner review is required. `.github/CODEOWNERS` designates
`@davidgmbb` for workflows/action pins, build/bootstrap policy, tooling and
native-retirement authority, generated assembly state, and agent instructions.
Ownership takes effect only after CODEOWNERS lands on main. GitHub forbids
self-approval: a davidgmbb-authored PR needs another eligible reviewer; once
CODEOWNERS lands, sensitive changes by that author need another designated
owner with write access. No second reviewer was configured by this change.

All six checks below are bound to the observed GitHub Actions app ID 15368.
Strict checks require the PR branch to be current with main. Deletion and
non-fast-forward updates remain forbidden.

| Required context | Workflow |
| --- | --- |
| CI complete | ci.yml |
| Linux x86-64 bootstrap evidence | self-host-audit.yml |
| Canonical TCC bootstrap | tcc-bootstrap.yml |
| GPU Linux consumers | gpu-toolchains.yml |
| Benchmark service workflow policy | bench-service-policy.yml |
| API migration policy | api-migration-policy.yml |

The aggregate covers lint, desktop, native, mobile, UEFI and analyzer shards
and the exact job inventory. Every dependency must succeed. Independent
required jobs fail explicitly when GH_ACTIONS_CI_ENABLED is unset or false;
the aggregate runs despite disabled/skipped dependencies and rejects them.
GitHub itself accepts skipped/neutral checks, so required status settings alone
cannot enforce this distinction. Keep the job guards and review policy intact.
Binding the source app does not uniquely bind a workflow; owners must reject
changes that replace validation with a same-name check or weaken these gates.

GPU Metal remains optional: it needs a configured private runner and excludes
fork PRs. Throughput workflows are diagnostic/qualification workflows with
separate event and revision policies, not general admission checks. Path-filtered
native-retirement, mobile lifecycle and other focused workflows are not blanket
requirements; making them mandatory needs an always-emitted, fail-closed
applicability result and merge-group support first. None substitutes for the
six unconditional gates above. This change does not enable merge queue or
broaden private-runner access.

## Verification and remaining acceptance gates

- Read-back of `/rules/branches/main` confirms all rules and check sources;
  `/branches/main` reports `protected: true`. The legacy protection subobject
  remains disabled and is not the authority for rulesets.
- Existing same-repository PR #897 reports `BLOCKED` and `REVIEW_REQUIRED`
  after the settings update. No production merge/push rejection was attempted.
- `python3 tests/ci_tools_test.py -v`: 63 tests pass, including the executable
  aggregate outcome fixture (625 combinations plus independent UEFI/analyzer
  failures) and enabled/disabled/missing-variable guards for all independent
  required jobs. These are offline policy tests, not a live cancellation race.
- Pinned actionlint passes across `.github/workflows/*.yml`.
- All six workflows have unfiltered pull_request and merge_group triggers;
  fork and same-repository PRs use hosted runners for required validation.
  CI and self-host audit validate the merge revision; TCC retains its existing
  explicit PR-head checkout. Merge-group events use the group revision.
- No live fork PR or merge-group execution was performed. No fork PR appeared
  in the most recent 100 PRs inspected. Queue execution must be validated when
  queueing is enabled; configuration and fixtures do not prove live behavior.
- The ownership/guard patch must merge and a second independent reviewer must
  be available before #556 can be considered complete. Recheck live results
  on the submitted revision; do not treat these local fixtures as full CI.

## Emergency procedure

There is no standing bypass. An emergency requires an explicitly authorized
administrator to record the reason and exact temporary settings change, retain
before/after evidence, and restore and verify the admission policy immediately
afterward. This document grants no permission to weaken protection. Prefer a
reviewed repair PR. Never replace cancelled, failed, missing, or outdated CI
with a manual success status.
