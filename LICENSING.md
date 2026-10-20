# Licensing

## Grant and scope

Copyright (c) 2026 David Gonzalez Martin, for his original contributions.
Copyright in third-party material remains with its respective rightsholders.

The original source, tests, tooling, configuration, examples and documentation
identified in the inventory below are licensed under
**Apache-2.0 WITH LLVM-exception**. The complete, unmodified Apache License 2.0
and LLVM exception are in [LICENSE](LICENSE). This scope statement is separate
from those legal texts.

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) contains the reviewed path and
portion inventory. Its component terms take precedence for the identified
material:

- The imported lexer family retains MIT and the applicable upstream Apache
  terms, plus the LLVM and Unicode notices identified for its scalar lexer and
  identifier tables. The plain Apache portions do **not** receive the LLVM
  exception.
- `lane.c`, including local changes, is provided under MIT with the retained
  RAD Debugger notice.
- The identified benchmark helpers retain MIT; the surrounding original
  benchmark code uses the project default.
- Copied license texts, the DCO and upstream attribution records retain their
  own terms. They are not relicensed as project prose.

Local modifications to identified third-party portions follow those portions'
terms. A file's inclusion in this repository does not override an upstream
license. There is no copyright assignment or additional commercial-use fee.
The licenses' conditions, including notices and any applicable patent terms,
still apply.

## Authority and adoption

The review covers all 30 tracked files at
[`76cab868a233fb81dac33933861a7b7d8d554649`](https://github.com/buster14a/buster-fast/tree/76cab868a233fb81dac33933861a7b7d8d554649),
plus the licensing documents, retained texts and source comments introduced
with this adoption. The inventory records additions since the issues' earlier
review pin.

On October 20, 2026, in the implementation discussion for
[#160](https://github.com/buster14a/buster-fast/issues/160),
[#161](https://github.com/buster14a/buster-fast/issues/161) and
[#162](https://github.com/buster14a/buster-fast/issues/162), David confirmed that
he is the sole contributor to the original buster-fast work, that other
contributor identities are agents acting for him, and that he has authority to
license those contributions under Apache-2.0 WITH LLVM-exception. The
[authority record](https://github.com/buster14a/buster-fast/issues/161#issuecomment-6287479170)
preserves that confirmation. The
maintainer's requested adoption also places his local component modifications
under the component terms above.

That confirmation supplies the historical first-party authority; Git author
names, agent trailers and repository ownership are not substitutes for it.
This is a grant made with adoption, not a claim that an absent license or DCO
certification existed at the time of earlier commits.

The prospective contribution policy becomes effective at the first commit on
`main` containing this document, `LICENSE`, `CONTRIBUTING.md` and `DCO`
together. The adoption PR's merge record identifies that exact commit and
date. Earlier branch commits are handled by the transition in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Compiler use, output and component reuse

Compiling independently authored user source does not by itself license that
source or its output under the compiler's license.

Distributing the compiler executable is distribution of the compiler and its
incorporated components. Directly copying or embedding its encoder, lexer,
linker or other components also requires complying with those components'
licenses. These activities are not generally exempted merely because the
software is a compiler.

The LLVM exception's first paragraph applies when compiling source embeds
portions of covered software into the object form of that source. For those
qualifying embedded portions it waives only Apache sections **4(a), 4(b) and
4(d)**. It does not waive section 4(c), grant the exception to Apache-only
third-party portions, or create a blanket exemption for copied source or
arbitrary component embedding. Its separate GPLv2 paragraph remains in
[LICENSE](LICENSE) with its own conditions.

There are no bundled production C library headers or runtime library at the
reviewed revision. `link_program.c` does emit original support routines and
data into synthetic test programs; these are covered original material, not
independently authored user programs. Tests can additionally link system
libc/startup files using the system toolchain. Those external files keep their
own terms. Any future bundled header, runtime or support implementation needs
its own provenance entry before being included in compiler output.

## Distributions

The current delivery path is the Git source tree and its single locally built
`build/compiler`; this change adds no binary packaging or release service.

For a full source distribution, retain `LICENSE`, this document,
`THIRD_PARTY_NOTICES.md`, the complete `LICENSES/` directory, and existing
source attribution and modification notices. Include `CONTRIBUTING.md` and
`DCO` when distributing the contribution instructions.

For a compiler binary distribution, accompany it with `LICENSE`,
`THIRD_PARTY_NOTICES.md` and the applicable `LICENSES/` files in accessible
documentation. Shipping the whole directory is the simple default. Preserve
applicable notices for system/runtime libraries if those are bundled too.
For a component-only distribution, use the portion map to select its terms and
notices; retain notices in any distributed source and mark modifications where
required. `THIRD_PARTY_NOTICES.md` supplies attribution information, not new
license conditions.
