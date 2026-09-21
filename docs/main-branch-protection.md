# Main admission policy

Settings rechecked 2026-09-20 UTC against main `d63af3bf` for #556.
The active repository ruleset 22537199 explicitly includes `refs/heads/main`,
excludes nothing, and has no bypass actors. The adjacent JSON files preserve
the earlier before/after settings and effective branch-rules response; they contain no
credentials. Repository rules, rather than legacy branch protection, enforce it.

## Required review and checks

Every change must use a PR, with zero required approvals and resolved review
threads. Mandatory code-owner approval, latest-push approval, stale approval
dismissal, and extra approval for unattributed changes are disabled. This is
the explicitly selected solo-maintainer policy: the author can inspect the
changes and merge when required validation passes, without another account.

`.github/CODEOWNERS` designates `@davidgmbb` for workflows/action pins,
build/bootstrap policy, tooling and native-retirement authority, generated
assembly state, and agent instructions. It records responsibility and routes
reviews; it is not an enforced independent-review barrier. GitHub forbids
self-approval, so independent review requires adding another maintainer and
revisiting this policy. The initial one-approval setting was corrected for
solo development; `ruleset-before-solo.json` preserves that intermediate state.

All six checks below are bound to the observed GitHub Actions app ID 15368.
The live ruleset permits a PR branch behind main (`strict_required_status_checks_policy: false`); all six required checks must still pass. The adjacent snapshots retain the earlier strict setting. Deletion and non-fast-forward updates remain forbidden.

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
cannot enforce this distinction. Keep the job guards and admission policy intact.
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
- The effective PR rule reports zero required approvals and no mandatory
  code-owner/latest-push approval after the solo-maintainer correction. PR #898
  no longer reports `REVIEW_REQUIRED`. No production merge/push rejection
  was attempted.
- `tests/ci_tools_test.py` retains the executable aggregate outcome fixture
  (625 combinations plus independent UEFI/analyzer failures). The enabled,
  disabled, missing and invalid-value guards for all independent required jobs
  run in `python3 tools/ci_admission_test.py -v`, including absolute Git Bash
  selection on Windows. Keeping the admission tests outside the frozen corpus
  preserves its reviewed bytes. These are offline policy tests; live
  cancellation-race validation remains separate.
- Pinned actionlint passes across `.github/workflows/*.yml`.
- All six workflows have unfiltered pull_request and merge_group triggers;
  fork and same-repository PRs use hosted runners for required validation.
  CI and self-host audit validate the merge revision; TCC retains its existing
  explicit PR-head checkout. Merge-group events use the group revision.
- No live fork PR or merge-group execution was performed. No fork PR appeared
  in the most recent 100 PRs inspected. Queue execution must be validated when
  queueing is enabled; configuration and fixtures do not prove live behavior.
- The ownership/guard patch must merge; live fork and merge-group validation
  remain outstanding for #556. Independent review is deliberately excluded
  from the solo-maintainer policy. Recheck live results on the submitted
  revision; do not treat these local fixtures as full CI.

## Emergency procedure

There is no standing bypass. An emergency requires an explicitly authorized
administrator to record the reason and exact temporary settings change, retain
before/after evidence, and restore and verify the admission policy immediately
afterward. This document grants no permission to weaken protection. Prefer a
repair PR with passing required validation. Never replace cancelled, failed, missing, or outdated CI
with a manual success status.
