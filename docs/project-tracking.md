# Project and feature tracking

[Catalogue](../PROJECTS.md) · [Agent instructions](../AGENTS.md) · [GitHub setup](github-project-setup.md)

## One home for each fact

| Fact | Authoritative home |
|---|---|
| Project identity, source home and documentation entry point | [PROJECTS.md](../PROJECTS.md). |
| Existing behavior, usage and limitations | The project's linked README/feature documentation; detailed contracts stay in their current guides. |
| Proposed work, acceptance, ownership, blockers and implementation | The owning GitHub issue and linked PRs. |
| Priority and delivery-stage overview | One GitHub Project named **Buster**, using those same issues/PRs. |
| Observed validation | Existing test/evidence outputs and PR reports tied to their actual revision/configuration. |

The catalogue is not a second backlog, and an issue is not a permanent inventory
of everything a product can do. Close completed work while retaining the feature
documentation. Do not mirror live status into repository snapshots or add a new
service, generator or CI gate merely to maintain this index.

## Features, tasks and shared work

A feature describes useful observable behavior for a person or another component.
An implementation task describes a change needed to deliver it. Give a substantial
feature one owning issue with scope, exclusions and acceptance criteria. Use its
existing issue number as the work identity; a stable documentation heading/feature
ID is enough for the lasting capability identity. Do not invent another ID registry.

Use sub-issues for independently actionable parts and **blocked by** relationships
for prerequisites. A shared component has one primary Area and linked consumers;
consumer-specific migrations can be separate tasks. A containing feature does not
own every shared dependency. Preserve existing branches and handoffs before edits.

An idea is not a commitment. New proposals start in Backlog, with Priority unset
until a maintainer decision; source presence or a merged prototype alone does not
establish support. Small experiments need only purpose, a real invocation and
limitations, not a product roadmap. Keep the existing
[research lifecycle](agents/research.md) and its evidence labels intact.

## Minimal work metadata

| Field | Values and meaning |
|---|---|
| **Area** | One stable Area ID from PROJECTS.md; add an area only for a coherent project/component, not a file or temporary campaign. |
| **Kind** | `Feature`, `Bug`, `Improvement`, `Research`, `Maintenance`. A feature is a usable outcome; mechanical work can be Improvement or Maintenance. |
| **Status** | `Backlog`, `Ready`, `In progress`, `Review/validation`, `Done`. These describe delivery stage, not proven product support. |
| **Priority** | `Now`, `Next`, `Later`; unset means untriaged, not lowest importance. Preserve explicit maintainer choices. |

Area/Kind/Status/Priority live in Project fields once configured, not a new parallel
label family. Existing labels retain their meanings; avoid mass relabeling. The
existing research `status/*` labels describe the immediate **evidence gate**, not
delivery stage, so they remain visible and are not replaced by Project Status.
Issue-form Area/Kind values are intake hints; after triage the Project fields are
authoritative and historical issue text need not be rewritten to match them.

**Ready** means the next bounded action is clear and no known blocker prevents it.
**In progress** requires an identified owner and active scope/branch.
**Review/validation** means implementation is awaiting applicable review, integration
or evidence. A blocker is a relationship and short next-action note, not a sixth
stage. If Ready work becomes blocked, move it back to Backlog; ongoing work can
retain its stage with the blocker explicit. Do not claim that an unresolved physical
run is an implementation task still available for another agent to take over.

**Done** means this issue's own acceptance is met and applicable changes are merged,
not simply that a linked PR closed. Close rejected/duplicate proposals with the
proper reason and successor; their closed state is not a delivered feature. Avoid
automatically marking a broad feature Done when only one child PR merges.

## Ordinary workflow and handoff

Before starting, locate the project in the catalogue, read its contract and search
open/closed issues and PRs. Reuse the relevant issue. Record the owner and bounded
scope before overlapping work; one writer owns each branch.

During work, put durable commits and useful checkpoints on the PR/issue. Changes
to observable behavior update the affected feature contract or limitation in the
same PR. An internal refactor without a capability change needs no catalogue edit.
Add an entry only for a new maintained project or a changed integration boundary.

Before handing off, leave the information needed to resume without chat history:

```text
Area / owning issue:
Owner and scope:
Branch / PR / exact source revision:
Completed change:
Validation actually run (commands/configuration/results or evidence links):
Unresolved findings and blockers (link their existing issues):
Next concrete action and who can perform it:
```

Use existing evidence links rather than copying large logs. Keep a support contract
separate from its most recent observation: a regression against supported behavior
is a defect, not permission to quietly downgrade the promise. Missing execution,
failed checks, expected rejection and not-applicable cases are not interchangeable.

Use the lightest validation appropriate to the change. Documentation-only updates
need link/command/whitespace checks, not compiler benchmarks. Compiler retirement,
service deployment and physical-host admission keep their existing contracts;
this tracking system neither expands nor bypasses them. A Project permission gap
does not block code work: record it on the owning issue, leave a clear handoff and
continue independent authorized work.

## Working views

Keep four starting views: **Feature roadmap**, **Ready work**, **Active and blockers**,
and **Per-area backlog**. Their filters/columns and one-time configuration are in
[GitHub setup](github-project-setup.md). Use a table for the roadmap until actual
dates exist; do not manufacture due dates or a monorepo-wide completion percentage.

Classify active work as it is touched. Do not reopen completed issues, copy the
historical backlog into new tickets, or require exhaustive classification before
another feature can ship.
