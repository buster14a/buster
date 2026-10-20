# Contributing

Read [AGENTS.md](AGENTS.md) for issue ownership, build/test instructions and
the shared benchmark host, and follow [the C style](docs/coding-style.md).

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

For agent-assisted work, the responsible human contributor reviews the work
and certifies their right to submit it. An agent must not invent a human
sign-off, sign on behalf of someone who has not actually certified the work,
or treat its own co-author trailer as consent. Passing through another
person's work also requires satisfying the applicable DCO clause.

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

This also applies to the two PRs open at the review pin,
[#99](https://github.com/buster14a/buster-fast/pull/99) and
[#159](https://github.com/buster14a/buster-fast/pull/159), if they cross the
effective point. This document itself provides no certification for them.
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
