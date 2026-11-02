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
  in that checkout with `sh .githooks/enable-sign-off.sh --maintainer-session`.
  It records `git config --local buster.dcoSignOff davidgmbb`; ordinary session
  startup never creates that consent on its own. Only he or his directed
  sessions may record it. Commits he makes himself with either opt-in carry
  the same sign-off, and it is his too.
- **Repository:** this repository only, including under a later name.
- **Time:** from the first commit on `main` that contains this section; the
  merge record of the PR that adds it names that commit. Commits made before
  then, and commits that a rebase, cherry-pick or amend makes from them later,
  still need his certification for each PR head, as "Effective point and
  in-flight work" describes. The direct-session and persistent-checkout
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
of theirs may add his sign-off. Do not reuse a maintainer-enabled checkout for
another contributor's submissions without disabling the opt-in.

### Automatic mechanics and revocation

For local Git, the existing session-start hooks install `.githooks` when an
opt-in is already present. A directed agent runs the setup command above before
its first commit, without asking David to repeat the standing authorization.
The checkout setting is untracked and shared with linked worktrees; it does not
follow a clone or authorize another repository. Existing custom hooks are not
replaced. The setup diagnostic tells an authorized session when it must append
the trailer itself instead.

The hook checks a present `BUSTER_DCO_SIGN_OFF` first: only `davidgmbb` enables
it; `off`, an empty value, or any other value disables it. When the variable is
absent, only the **local** `buster.dcoSignOff` setting is consulted, not global
Git configuration. An explicit local value other than `davidgmbb` disables that
fallback, and session setup never overwrites it. Existing environment-only
setups remain temporary: normal startup does not persist their opt-in.

Use `BUSTER_DCO_SIGN_OFF=off git commit ...` to suppress automatic insertion
for one operation. To disable a persisted checkout opt-in, run
`git config --local buster.dcoSignOff off` and remove any enabling environment
variable (or set it to `off`). Merely unsetting the variable is not enough when
the checkout setting enables it. David may explicitly re-enable a checkout with
`git config --local buster.dcoSignOff davidgmbb`.

An explicit instruction from David not to sign, or an explicit environment or
checkout opt-out, must not be bypassed by manually adding his trailer. A direct
session instruction does not silently undo revocation. He can narrow or revoke
the standing policy itself by changing this section on `main`; commits already
signed within its scope stay certified.

GitHub API/connector commits do not run local Git hooks. An authorized directed
session adds the exact trailer above to each new commit message itself, once,
after its other trailers, preserves the true author/committer attribution, and
reads back the stored commit message to verify it. Lack of a shell or of the
environment variable is not, by itself, a reason to omit an authorized sign-off
or ask for another routine certification. A PR-body statement is not a substitute
for those commit trailers. Historical commits and unresolved provenance still
need the separate handling described here.

The isolated shell-hook regression fixtures run in the path-scoped `DCO hooks`
GitHub Actions workflow; they build no compiler and change no branch protection.
They can also be run with `sh .githooks/test-sign-off.sh` on the shared host under
the normal lock. They exercise only disposable repositories, not project history.

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
