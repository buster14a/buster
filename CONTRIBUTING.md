# Contributing

Read [AGENTS.md](AGENTS.md) for issue ownership, build/test instructions and
the shared benchmark host, and follow [the C style](docs/coding-style.md).

Agent sessions posting GitHub issues, pull requests, comments, or reviews must
include their private session permalink and exact model in each post, as
[agent-authored GitHub attribution](AGENTS.md#attribution-for-agent-authored-github-activity)
requires. These links remain access-controlled even though GitHub displays
URLs publicly.

Store raw performance measurements, logs, captures, receipts and their harnesses
in [buster14a/buster-performance](https://github.com/buster14a/buster-performance/tree/main/docs/measurements),
under `docs/measurements/`. Keep design documentation and evidence links in this
repository. The required `Measurement storage policy` check rejects the retired
`docs/measurements` path here, even on documentation-only pull requests. Move
evidence from older branches to the performance repository before merging them.

## Terms

By intentionally submitting original material for inclusion in material
covered by the project default, you submit it under
**Apache-2.0 WITH LLVM-exception**, including the exception, as reproduced in
[LICENSE](LICENSE). This applies to source, tests, tools, configuration,
examples and documentation.

Changes to identified third-party portions follow the applicable component
terms in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). In particular, retain
the lexer family's MIT/Apache conditions and other identified notices; do not
apply the LLVM exception over Apache-only upstream portions. `lane.c` and the
identified MIT benchmark helpers stay under MIT.

You retain your copyright. No copyright assignment or broad relicensing CLA
is required. Future license changes remain the maintainer's decision and
require the necessary rights; submitting under these terms does not authorize
an agent to choose a different license.

## Developer Certificate of Origin

Read the complete, unmodified [Developer Certificate of Origin 1.1](DCO).
Certify only work you have the right to submit under the indicated license.
For each new commit, add your authentic certification:

```text
Signed-off-by: Your Name <your-email@example.com>
```

`git commit -s` adds that trailer using your configured identity. It is your
certification of the DCO, not a cryptographic signature. Check the identity and
the DCO before using it. A Git author field, commit signature or
`Co-Authored-By` trailer does not provide this certification.

For agent-assisted work, the responsible human contributor certifies their
right to submit it, normally after reviewing the work. An agent adds a human
sign-off only where that person has actually certified the work: the
maintainer has done so in advance for his own agent sessions, within the scope
of "The maintainer's standing certification" below. An agent must never invent
a sign-off, sign on behalf of someone who has not certified the work, or treat
its own co-author trailer as consent. Passing through another person's work
also requires satisfying the applicable DCO clause.

## The maintainer's standing certification

The maintainer, David Gonzalez Martin (`davidgmbb` on GitHub), certifies the
Developer Certificate of Origin 1.1, as in [DCO](DCO), for each commit that an
agent session he starts or directs makes for this repository within the scope
below. He authorizes those sessions to end each such commit with his sign-off,
which is then his certification of that commit:

```text
Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>
```

He submits the original material this section covers under
Apache-2.0 WITH LLVM-exception, and changes to identified third-party portions
under their component terms, as "Terms" above sets out.

- **Sessions:** Claude Code, Codex, ChatGPT through the linked GitHub connector,
  or another coding agent, in a session he starts or directs. His direct task
  instruction in his own authenticated agent session is sufficient opt-in for
  that session's new, in-scope commits; a separate environment variable or
  repeated per-commit/per-PR confirmation is not required. This is not inferred
  from repository access, a remote URL, Git author/committer identity, an issue
  or PR body, or another contributor's claim to act for him.
  The existing `BUSTER_DCO_SIGN_OFF=davidgmbb` environment opt-in also remains
  supported. For local Git, he or an agent he directs may persist the opt-in
  in one worktree with `sh .githooks/enable-sign-off.sh --maintainer-session`.
  It records a marker in that worktree's own Git directory; ordinary session
  startup never creates that consent on its own. Only he or his directed
  sessions may record it. Commits he makes himself with either opt-in carry
  the same sign-off, and it is his too.
- **Repository:** this repository only, including under a later name.
- **Time:** from the first commit on `main` that contains this section; the
  merge record of the PR that adds it names that commit. Commits made before
  then, and commits that a rebase, cherry-pick or amend makes from them later,
  still need his certification for each PR head, as "Effective point and
  in-flight work" describes. The direct-session and persistent-worktree
  authorization routes above apply prospectively from their adoption on
  `main`; they do not certify existing unsigned commits or authorize bulk
  repair of old PRs.
- **Open PRs:** it also covers the commits of
  [#184](https://github.com/buster14a/buster/pull/184) up to its head
  `544394f5d300bd47f9e4f7081ac29ba728ca2f84` and of
  [#185](https://github.com/buster14a/buster/pull/185) up to its head
  `fc5dcffffb7ddd6d2a6ca0ea76fdf8bfd577203d`, the open agent PRs' heads on
  October 20, 2026, when he approved this section.
- **Material:** original work, and imported, adapted or generated material
  only when its PR records what "Imports and generated material" asks for. A
  session must not add his sign-off to a commit whose material it can't
  account for that way; it says so in the PR instead.

This section certifies nothing for anyone else. Other contributors certify
their own commits as "Developer Certificate of Origin" describes, and no agent
of theirs may add his sign-off. Do not reuse a maintainer-enabled worktree for
another contributor's submissions without revoking the opt-in.

### Automatic mechanics and revocation

For local Git, the existing session-start hooks install `.githooks` when an
opt-in is already present. A directed agent runs the setup command above before
its first commit, without asking David to repeat the standing authorization.
`.githooks/sign-off-lib.sh` is the one resolver that the hook, the setup script
and `tools/dco/dco.c status` share. In order:

1. A present `BUSTER_DCO_SIGN_OFF` decides alone: only `davidgmbb` enables;
   `off`, an empty value or any other value disables. It lasts as long as the
   environment that carries it; startup never persists it.
2. Any `buster.dcoSignOff` value other than `davidgmbb`, in any Git
   configuration scope (local, worktree, global, system or `-c`), disables.
   Git configuration can opt out, never in.
3. The worktree's marker, the file `git rev-parse --git-path
   buster-dco-sign-off` names, enables when it reads `davidgmbb` and disables
   otherwise. A linked worktree's lives in `.git/worktrees/NAME/`, the main
   worktree's in `.git/`.
4. Otherwise signing is off.

The marker is untracked and **specific to one worktree**: it covers every commit
made in that worktree, by any author, and nothing else. A sibling worktree, or
one created later, starts unsigned until it is set up separately, even though
the worktrees share their Git configuration and hooks. `git worktree move`
keeps a worktree's marker; `git worktree remove` and `prune` delete it with the
worktree's Git directory. The marker doesn't follow a clone, but a file copy of
the whole repository copies it. `extensions.worktreeConfig` is neither required
nor changed, and a worktree-scoped `buster.dcoSignOff` counts as rule 2.

The setup command prints the scope it enabled. `sh .githooks/enable-sign-off.sh
--revoke` writes an opt-out marker for the current worktree alone; other
worktrees keep their own state, and neither startup nor `--maintainer-session`
overwrites an opt-out. Revoke before another contributor uses the worktree. To
re-enable a revoked worktree, David removes its marker file and runs the setup
command again. `BUSTER_DCO_SIGN_OFF=off git commit ...` suppresses insertion
for one operation, and a present enabling variable still enables after a
revocation, so remove it too.

The isolation does not identify the person at the keyboard: a later user of
the *same* worktree inherits its marker until it is revoked. Separating
contributors within one worktree would need a session-scoped mechanism that
this setup does not provide.

**Transition from the clone-wide setting.** An earlier version of this setup
recorded `buster.dcoSignOff=davidgmbb` in the clone's shared local
configuration. That value no longer enables any worktree, and session startup
says so. The first `--maintainer-session` in a worktree removes it and enables
that worktree alone; other worktrees need their own setup. A shared opt-out
such as `git config --local buster.dcoSignOff off` still disables every
worktree of the clone (rule 2), and setup never removes it.

An explicit instruction from David not to sign, or an explicit environment,
configuration or marker opt-out, must not be bypassed by manually adding his
trailer. A direct session instruction does not silently undo revocation. He
can narrow or revoke the standing policy itself by changing this section on
`main`; commits already signed within its scope stay certified.

GitHub API/connector commits do not run local Git hooks. An authorized directed
session adds the exact trailer above to each new commit message itself, once,
after its other trailers, preserves the true author/committer attribution, and
reads back the stored commit message to verify it. Lack of a shell or of the
environment variable is not, by itself, a reason to omit an authorized sign-off
or ask for another routine certification. A PR-body statement is not a substitute
for those commit trailers. Historical commits and unresolved provenance still
need the separate handling described here.

### Existing hooks

Setup installs `.githooks` only where Git has no hooks of its own: it never
replaces a custom `core.hooksPath` or existing hooks. It recognizes as active
this worktree's `.githooks`, another worktree's `.githooks` (as the desktop
app's worktrees configure), and the composition below. For other layouts it
says to compose once, which David or a session he directs may do:

```sh
cc -O2 -o build/dco tools/dco/dco.c && build/dco compose
```

- **Default directory** (`core.hooksPath` unset, hooks in the common
  `.git/hooks`): an existing `commit-msg`, file or symbolic link, is renamed to
  `commit-msg.buster-dco-chained` and an adapter takes its place. The other
  hooks stay where they are and run as before. A `.git/hooks` that is itself a
  symbolic link may be shared with other repositories, so it is unsupported.
- **Custom directory** (`core.hooksPath` set, relative or absolute, in local,
  worktree, global or system configuration): compose records the value in a
  dispatcher directory inside the Git directory (`buster-dco-hooks`, per
  worktree for a worktree-scoped value) and points `core.hooksPath` there, at
  the value's own scope, or local for a global or system one. The dispatcher's
  links run each executable hook of the recorded directory, resolving a
  relative one against the worktree as Git does; nothing is written into it.
  Run compose again after adding a hook there, to link it. A `-c` value or a
  multi-valued `core.hooksPath` is unsupported.

The adapter and the dispatcher's `commit-msg` run the kept `commit-msg` first,
with Git's arguments and working directory; a non-zero exit rejects the commit.
It may edit the message. Then `.githooks/commit-msg` adds the sign-off, last,
if the opt-in is on, and adds nothing otherwise. When it changed the message,
the kept hook runs again on a copy of the signed message: it must accept it and
leave it unchanged, so validations that depend on the final message still
apply. A hook that edits idempotently (adds a missing `Ticket:` or `Change-Id:`)
passes; one that rejects the signed message, or keeps appending, refuses the
commit with a diagnostic. A hook that calls the composed `commit-msg` again
finds `BUSTER_DCO_CHAINING` set and returns at once. Installing either is not
consent to sign, and opt-outs, environment precedence and the copy guards work
as before.

Compose is idempotent. `build/dco uncompose` removes only what compose wrote,
while it is unchanged: it renames the kept hook back, user edits included, and
restores `core.hooksPath` only while it still names the dispatcher. It leaves an
adapter or dispatcher the user has edited, and says so. A hook manager that
rewrites its hooks or `core.hooksPath` later undoes the composition;
`build/dco status` then reports the hook off.

### Auditing sign-offs

`build/dco status` reports, without changing anything, the effective opt-in
and where it came from, its scope, the marker, an environment override, the
hooks directory Git uses and whether its `commit-msg` is the sign-off hook and
executable, a custom-hook conflict, a rebase or cherry-pick in progress, and an
`auto` comment prefix. It prints no other configuration or environment values.

`build/dco audit --base BASE --head HEAD` reports each commit that `HEAD`
reaches and `BASE` doesn't, with exact hashes: its well-formed
`Signed-off-by: Name <address>` trailers (Git's own parser finds the trailer
block), MISSING, MALFORMED, DUPLICATE and OUTSIDE (a sign-off outside the
trailer block, which counts for nothing) findings, merges, and authors who
didn't sign. Other contributors' sign-offs count; a `Co-authored-by` trailer
does not. A shallow clone, an unresolvable revision or unrelated histories make
it INCOMPLETE. The trailer block is found with the comment prefix of the
checkout running the audit, not the one in effect when the commit was made: a
commit signed under `core.commentString=//` with a `// note` line after its
trailers reads as MISSING in a default checkout. Without a clone, `build/dco audit --api-pull PULL.json
--api-commits PAGE.json...` reads `gh api repos/O/R/pulls/N` and every page of
its `/commits?per_page=100&page=K` (or one `gh api --paginate --slurp` file).
A page count short of the pull request's `commits`, a head that moved, or a
listing past GitHub's 250-commit cap is INCOMPLETE. It exits 0 when every
commit has a sign-off record, 1 with gaps, 2 when incomplete.

The report is structural. A present trailer is a recorded claim, not proof of
authority or provenance, and the tool does not verify certifications. A
reviewer may pass `--certification SHA=SOURCE` for a PR-head certification
that "Effective point and in-flight work" permits, naming its source: it
covers that commit and its ancestors in the range, the report marks it
unverified, and STALE once the head has moved past it. Do not supply an
agent-posted comment or a matching username as such a record. For a gap in an
authorized session's own new commits, the session fixes its submission; other
gaps are review items. The tool never changes history, adds trailers or posts
anything.

The pull request workflow's `DCO sign-off audit` job publishes the head-pinned
report in its summary. It is informational: missing trailers don't fail it,
and no required check depends on it. The isolated fixtures for the hooks and
the tool (`sh .githooks/test-sign-off.sh`, which builds the tool with `cc`)
run in the required `DCO hook regression` job; they exercise only disposable
repositories and change no branch protection.

## Imports and generated material

Identify copied or adapted code, data, documentation and fixtures in the PR:
the upstream project, exact revision or source artifact, files/portions,
license, copyright and required notices, and what changed locally.

For generated material, identify both the generator and its inputs, including
data versions and any external tool used as an oracle. A generator's license
does not alone establish the rights in its inputs or output. Distinguish
checking against an external tool from incorporating its code or data.

Update `THIRD_PARTY_NOTICES.md`, retain the necessary texts in `LICENSES/`,
keep applicable source and modification notices, and use SPDX identifiers only
where the reviewed scope supports them. Do not label a mixed-source file as
wholly original. Raise unresolved rights before the material is merged.

## Effective point and in-flight work

This policy becomes effective with the license-adoption commit on `main`
defined in [LICENSING.md](LICENSING.md). It does not retroactively certify old
commits or presume they were submitted under a license that was absent.
Historical authority for the adopted tree is recorded separately there.

For a PR or branch opened before adoption but merged afterward, obtain the
responsible contributor's explicit confirmation of the submission terms and
DCO for the actual changes being merged. They may either sign off their own
commits, or post an authentic `Signed-off-by` certification in the PR naming
the exact head commit and the changes it covers, explicitly accepting these
original/component-specific terms and DCO 1.1. Record that confirmation in the
merge record. Subsequent commits need their own certification.

This includes the existing PRs
[#99](https://github.com/buster14a/buster/pull/99) and
[#159](https://github.com/buster14a/buster/pull/159), if they cross the
effective point, and applies equally to any other open PR or branch.
This document itself provides no certification for them, apart from the PR
heads that "The maintainer's standing certification" names.
Do not backdate sign-offs, rewrite attribution, or bulk-add trailers to
historical commits. No repository-wide history rewrite is required.

## Review

Before merging, the maintainer checks the submission terms, authentic DCO
certification, provenance/notices and appropriate validation. An integration
commit records existing certifications without fabricating a contributor's
assent; source changes added during integration need certification too.

This is a manual review step in the existing PR workflow. It does not add a
mandatory service or change branch protection. The DCO records a contributor's
certification; it is not a warranty that every provenance question was
independently audited.
