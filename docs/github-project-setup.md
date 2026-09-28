# GitHub Project setup

[Tracking conventions](project-tracking.md) · [Catalogue](../PROJECTS.md)

This is the desired configuration, not evidence that a board already exists.
Implementation/readback progress and the actual Project URL are tracked in
[#1677](https://github.com/buster14a/buster/issues/1677). Keep that live state there.

## Create or reuse one project

Use an account authorized to administer Projects for the repository owner. Inspect
the projects linked to `buster14a/buster` and the owner's projects first. Reuse an
existing suitable **Buster** project; do not create another merely because a connector
cannot see it. Link the chosen project to this repository. Do not change repository
visibility, permissions or branch protections to configure a tracker.

In the Project settings, configure single-select **Area**, **Kind** and **Priority**
from [the tracking convention](project-tracking.md#minimal-work-metadata). Use the
Area IDs in PROJECTS.md rather than a second hand-maintained label vocabulary.
Configure the existing **Status** field with the five delivery stages; do not create
a second Status field. On an existing board, inspect current options and map existing
items deliberately before renaming/removing any option. Leave untriaged Priority
unset. Keep Assignees, Labels and linked-PR/blocker information available as native
metadata rather than copying them into text fields.

The optional GitHub CLI route uses the officially documented `gh project` commands.
An authenticated owner-side session needs Projects access; a repository-only token
is not enough. Do not paste a token into an issue, PR or chat. Inspect first:

```sh
gh project list --owner buster14a --format json
```

Only after establishing that no suitable project exists:

```sh
gh project create --owner buster14a --title Buster --format json
```

Use the actual returned owner/number for subsequent `gh project link`,
`field-list`, `field-create` and `item-add` operations. Read fields before creating
them and read back items after editing them; commands in this guide are not a
claim of execution. The web interface also supports the full setup without a CLI.

## Save the working views

Filters below use the exact field names/options in the tracking convention.
Save each view after setting its filter, grouping, sorting and displayed columns.

| View | Layout and filter | Useful presentation |
|---|---|---|
| Feature roadmap | Table: `is:issue is:open kind:Feature` | Group by Area; show Status, Priority and Assignees. No invented dates. |
| Ready work | Table: `is:open status:Ready` | Show Area, Kind, Priority, Assignees and dependencies. Only genuinely unblocked next actions belong in Ready. |
| Active and blockers | Table: `is:open status:Ready,"In progress","Review/validation"` | Show Area, Status, Assignees, Labels and native blocker/linked-PR information. Inspect blocked backlog items through the per-area view as well. |
| Per-area backlog | Table: `is:open` grouped by Area | Filter by `area:foundation`, `area:compiler`, or another catalogue ID when focusing; save separate area tabs only when useful. |

Keep cancelled/not-planned outcomes distinguishable from delivered features. Do not
install an automation that closes a parent feature or marks it Done merely because
one linked PR merged. Optional auto-add should add repository issues for triage,
not duplicate each issue into a draft item or silently assign Ready/Priority.

## Seed existing work and verify

Start with existing issues rather than creating empty feature epics for every area.
[#1677](https://github.com/buster14a/buster/issues/1677) records the initial seed
suggestions. Re-read each issue and active PRs before setting its Status; a stale
body, open badge or missing assignee is not proof that work is Ready or unowned.
Preserve current owners, research lifecycle labels and maintainer priorities.

Verify the real project URL, repository link, field options, saved views and seeded
item identities. Check that an item appears in its intended view without duplicating
its issue body, and that an untriaged issue does not accidentally appear as Ready.
Record the readback on #1677; inability to administer Projects is an explicit
remaining step, not a reason to claim completion or introduce another tracker.

## Reference

[Projects CLI](https://cli.github.com/manual/gh_project),
[field creation](https://cli.github.com/manual/gh_project_field-create),
[item addition](https://cli.github.com/manual/gh_project_item-add),
[view management](https://docs.github.com/en/issues/planning-and-tracking-with-projects/customizing-views-in-your-project/managing-your-views),
and [filtering](https://docs.github.com/en/issues/planning-and-tracking-with-projects/customizing-views-in-your-project/filtering-projects).
