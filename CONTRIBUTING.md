# Contributing

Read [AGENTS.md](AGENTS.md) for issue ownership, build/test instructions and
the shared benchmark host, and follow [the C style](docs/coding-style.md).

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

- **Sessions:** Claude Code, Codex or another coding agent, in a session that
  he starts or directs and that runs with his opt-in: the environment variable
  `BUSTER_DCO_SIGN_OFF=davidgmbb`, set in his own cloud environments and on his
  own computers. Only he sets it, and a sign-off added where anyone else set it
  is not his. Commits he makes himself where it is set carry the same sign-off,
  and it is his too.
- **Repository:** this repository only, including under a later name.
- **Time:** from the first commit on `main` that contains this section; the
  merge record of the PR that adds it names that commit. Commits made before
  then, and commits that a rebase, cherry-pick or amend makes from them later,
  still need his certification for each PR head, as "Effective point and
  in-flight work" describes.
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
of theirs may add his sign-off.

He revokes or narrows this certification by unsetting his opt-in, which stops
new sign-offs at once, and by changing this section on `main`, which records
it. Commits signed before then stay certified.

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
