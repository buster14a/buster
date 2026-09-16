# Third-party licenses and notices

This file records third-party material identified in Buster's documented
imports, generated metadata, adapted tests, hash integration, and retained
window code. Read it together with the full texts in [LICENSES](LICENSES/README.md).
The inventory was prepared against main
`68eaf7571c8bfaaf7c75bf617e22aa6f3297cd60` on 2026-09-14 for
[issue #620](https://github.com/buster14a/buster/issues/620).

**These are upstream notices, not a project-wide license for Buster.** No
first-party license is selected by this change. That decision, contribution
authority, and unresolved redistribution questions are tracked in
[issue #621](https://github.com/buster14a/buster/issues/621).
The applicable upstream terms are not replaced by this summary. A project
being scanned, used as a tool, or tested does not by itself make all of its
code part of Buster; conversely, generated or translated material must not
lose its source attribution just because the upstream implementation is not
linked into the compiler.

## Material included or adapted

Paths below are relative to the repository root. For assembly paths, `G`
means `src/buster/lib/compiler/assembly/generated/`. This is an inventory of
the identified sources, not a blanket license assignment to every file in a
directory or to all future contents of that directory.

| Project | Buster material / relationship | Upstream terms retained here |
|---|---|---|
| Intel XED | `G/x86_64-xed.jsonl`, `G/x86_64-assembly.generated.h`, `G/x86_64-coverage.generated.inc`: normalized and packed instruction metadata | [Apache-2.0](LICENSES/intel-xed-LICENSE.txt) |
| LLVM | `G/aarch64-llvm.jsonl` and the LLVM-derived assembly, coverage, form-ID, production-plan, missing-field and M1-profile projections described below | [Apache-2.0 WITH LLVM-exception](LICENSES/llvm-LICENSE.txt); complete upstream file, including legacy notices |
| Zig | `tests/c_abi.h`, `tests/c_abi_cfuncs.c`, `tests/c_abi_main.c`, `tests/c_abi_main_generated.c`: adapted/translated C ABI tests | [MIT](LICENSES/zig-LICENSE.txt) |
| xxHash | XXH64 algorithm credit and optional external xxHash integration in `src/buster/lib/hash.c` | [BSD-2-Clause upstream library notice](LICENSES/xxhash-LICENSE.txt) |
| XCB / xcb-proto | XCB manual-page text retained as comments in `src/buster/lib/window/xcb.c`, identifying `xproto.xml` as its source | [Exact source-header terms](LICENSES/xcb-xproto-LICENSE.txt), including the restriction on promotional use of authors' names |
| Arm architecture XML | Arm-derived ISA/canonical/semantic/system-register projections and mixed-source crosswalks in `G`; original archives are not vendored | **Restricted source; unresolved redistribution review.** See the dedicated section below; none of the open-source texts above grants rights to this material. |

### Intel XED

Upstream: [intelxed/xed](https://github.com/intelxed/xed), recorded release
`v2026.07.15`, commit `519c843c86547e2003f5a404a53358a7dcfb82f3`.
The pinned `datafiles/xed-isa.txt` carries this notice:

> Copyright (c) 2026 Intel Corporation

The complete pinned [upstream LICENSE](https://github.com/intelxed/xed/blob/519c843c86547e2003f5a404a53358a7dcfb82f3/LICENSE)
is reproduced locally. A root `NOTICE` file was not present at that revision;
the copyright notice above is retained from the instruction-data source.

Buster's imported outputs are not verbatim XED source files. Buster reduces
and normalizes instruction records, assigns its own compact representation,
and generates packed tables, indexes, accessors and coverage classifications.
The [assembly provenance manifest](src/buster/lib/compiler/assembly/generated/manifest.json)
records the declared source revision separately from the checked-in input
identity and currently sets `raw_snapshot_provenance=false`. Adding a license
copy does not turn that into verified raw-checkout provenance.

### LLVM

Upstream: [llvm/llvm-project](https://github.com/llvm/llvm-project), recorded
release `llvmorg-22.1.8`, commit
`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`.
The pinned AArch64 TableGen source identifies itself as:

> Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.

The complete pinned `llvm/LICENSE.TXT` is reproduced locally, including the
LLVM exceptions, the third-party-software section, and the legacy
University of Illinois/NCSA notice. Do not replace it with a generic Apache
license or treat the legacy section as permission to choose a different
license for the current imported records. No `llvm/NOTICE` file was present
at the inspected revision.

Buster reduces non-pseudo AArch64 records and generates its own normalized
JSONL, packed tables, indexes, accessors, coverage decisions and production
plans. The LLVM-derived outputs documented in the
[assembly metadata guide](src/buster/lib/compiler/assembly/generated/README.md)
include `aarch64-assembly.generated.h`, `aarch64-coverage.generated.inc`,
`aarch64-form-ids.generated.h`, `aarch64-production-plan.generated.h`,
`aarch64-production-plan.generated.jsonl`,
`aarch64-missing-fields.generated.jsonl`, and
`aarch64-apple-m1-profile.generated.jsonl` under `G`.
The manifest distinguishes the reduced JSONL from an independently verified
raw TableGen snapshot. **Other AArch64 files are not automatically LLVM
material:** Arm canonical/system-register data and mixed-source crosswalks
retain their separate provenance and review requirements.

### Zig ABI tests

Upstream: [ziglang/zig](https://github.com/ziglang/zig), revision
`3c46da14db` recorded in the [ABI prelude](tests/c_abi.h), specifically
`test/c_abi/cfuncs.c` and `test/c_abi/main.zig`.
The [upstream LICENSE at that revision](https://github.com/ziglang/zig/blob/3c46da14db/LICENSE)
is reproduced locally with its copyright notice:

> Copyright (c) Zig contributors

Buster replaces the hosted C include prelude, adds capability gates, and
translates the Zig side into C while retaining the original ABI test shape
and symbol correspondence. The [porting tool](tools/port_zig_c_abi.py)
regenerates `c_abi_cfuncs.c` and `c_abi_main_generated.c`; the manually
translated half is in `c_abi_main.c`. Future resynchronizations must update
the recorded upstream revision and retain the upstream notice even when
regenerating or reorganizing the files. This attribution is for the test
port, not a claim that the Zig compiler is linked or vendored into Buster.

### xxHash

Upstream: [Cyan4973/xxHash](https://github.com/Cyan4973/xxHash).
The [hash implementation](src/buster/lib/hash.c) uses XXH64's algorithm in
its default path; the optional `USE_XXHASH` path includes the external
xxHash header and calls `XXH3_64bits`. Buster maps a zero result to one for
its table-key convention.

The retained upstream library notice says:

> Copyright (c) 2012-2021 Yann Collet

The local license document is the exact `LICENSE` from upstream `v0.8.3`.
That is a **license-document source**, not an invented claim that Buster's
implementation was copied from that version or that an installed external
header has that version. The original implementation/import revision is not
recorded in `hash.c`. Preserve the actual external header's own notices
when enabling or distributing it. This entry credits the XXH64 algorithm
and documents the optional integration; it does not assign the xxHash
license to unrelated SHA-256 functions sharing the same file.

### XCB documentation comments

The retained XCB manual-page comments in
[src/buster/lib/window/xcb.c](src/buster/lib/window/xcb.c) identify their
origin as documentation generated from `xproto.xml` and retain the upstream
contact `xcb@lists.freedesktop.org`. The corresponding source-header license
has been copied in full from `src/xproto.xml` in the
[stapelberg/xcb-proto documentation fork](https://github.com/stapelberg/xcb-proto),
with source blob identity recorded in [LICENSES/README.md](LICENSES/README.md).
It includes the copyright of Bart Massey, Jamey Sharp, and Josh Triplett
and the authors' promotional-name restriction; it is not replaced by a
generic MIT template.

The exact original manual-page/package revision is not recorded in Buster's
comments. The documented source of this license copy is not asserted to be
that original revision. This entry applies to the retained documentation
text, not a claim that all of Buster's window implementation was copied from
XCB. Any separately linked or packaged X11/XCB libraries retain their own
version-specific notices.

## Arm XML inputs and derived projections: permission review remains open

The assembly guide and importers identify official Arm A-profile **ISA A64**
and **SysReg** XML release `2026-06`. The original archives/XML are restricted
and non-vendored, but **derived material is checked in**. That includes
`arm-a64-canonical.generated.jsonl`, its canonical decoder and semantic
projections, system-register projections, and the mixed LLVM/Arm
`aarch64-exact-crosswalk.generated.h`. Omitting raw XML is not treated here
as proof that all resulting distributions are permitted.

The [canonical decoder generator](tools/gen_aarch64_canonical_decoder.py)
and [system-register generator](tools/generate_aarch64_system_registers.py)
identify their inputs and outputs; retain the adjacent manifests and source
identities when updating them. The recorded official sources are:

- ISA A64: `ISA_A64_xml_A_profile-2026-06.tar.gz`, archive SHA-256
  `63a01a1696483bbe2edfef9e0f0cd053d6c1c619ec0587876cb7a60bb344f354`.
- SysReg: `SysReg_xml_A_profile-2026-06.tar.gz`, archive SHA-256
  `4795c769085ff9056d9f18abbd9e23d7b0f0a955214cfb2a2121a9698b50d509`;
  the recorded notice/provenance digest is
  `13a5c90f2accaf17573f73499a3940df6168d65cd17a893f685e686aa436a246`.

Exact source URLs and the release-specific provenance are retained in the
[assembly metadata guide](src/buster/lib/compiler/assembly/generated/README.md)
and [manifest](src/buster/lib/compiler/assembly/generated/manifest.json).
A notice digest is not the notice text or a permission grant. The applicable
Arm release terms have **not** been reproduced or legally cleared by this
notice-restoration change. No Apache, MIT or BSD license is assigned to
Arm-derived material here. Do not publish a new archive of the restricted
inputs or represent this inventory as approval for distributing the derived
outputs. Obtain and review the exact release terms and record the permitted
scope or a replacement/removal decision in
[issue #621](https://github.com/buster14a/buster/issues/621) before claiming
redistribution clearance for affected source or binaries.

## Separately obtained compatibility projects and tools

The [compatibility harness index](docs/agents/compatibility.md) documents
external inputs for cJSON, zlib, Lua, yyjson, stb, LZ4, SQLite, sbase,
DoomGeneric, QuickJS, Test262, musl, libc-test, and CPython. Those upstream
source trees are obtained separately; the guide explicitly forbids copying
or patching them into this repository. The harness index and each linked
project guide record the relevant input/pin and reproduction contract.
The license texts above are not substitutes for any of those projects'
licenses or for their bundled third-party notices.

Keep each external checkout's own `LICENSE`, `COPYING`, notices and
per-file terms intact. A test executable, copied regression, benchmark
corpus, source archive or binary dependency included in a release must be
inventoried separately and accompanied by the applicable terms; testing a
project does not automatically authorize redistributing it. This is
particularly important for multi-license projects, optional components,
and assets such as game data or fonts.

Similarly, the external compiler/build tools, system libraries and SDKs used
for a particular build retain their own terms. The optional external GPU
pipelines are described in the [project guide](docs/agents/project.md);
invoking an external shader compiler is distinct from bundling its code or
SDK. This inventory is not a complete bill of materials for an arbitrary
machine, installed toolchain, or locally built test artifact.

## Updating imports and preparing distributions

1. Before adding or updating copied, translated or generated material,
   identify the actual upstream files, revision, applicable per-file terms,
   copyright notices and any `NOTICE` file. Record the import relation and
   modifications, not just a project name or a mutable download URL.
2. Update this path inventory and retain the complete applicable license
   texts in `LICENSES/`. Record the license-document provenance and identity
   in its README. Do not strip existing notices, overwrite third-party terms
   with Buster's eventual license, or silently infer an unknown revision.
3. Review generator changes as provenance changes too. Keep attribution and
   modification notices with generated outputs or their accompanying
   distribution documentation, preserving per-file notices where required.
   Regeneration and source-file moves must not detach outputs from their
   applicable notices.
4. Include this inventory and the applicable `LICENSES/` texts with source
   distributions and with binary distributions containing the corresponding
   material. Preserve any other required notices, source offers or source
   availability obligations of separately bundled components. Inspect the
   actual archive/package contents; a successful compile is not a packaging
   or licensing check. This change does not modify packaging scripts.
5. Keep restricted or unresolved material explicitly unapproved. Resolve the
   Arm questions and Buster's first-party licensing decision before claiming
   general release clearance. Adding attribution alone does not settle
   copyright ownership, license compatibility or permission to redistribute.
