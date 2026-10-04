# Full machine-instruction encoding audit evidence

**Owning issue:** [buster14a/buster #2625](https://github.com/buster14a/buster/issues/2625)  
**Audited source:** `f34cc3a56296be1b1244647775e1a40a67dae3ef`  
**Audit and publication date:** 2026-10-04  
**Publication branch:** `codex/2625-encoding-audit-evidence`

This is an immutable evidence snapshot, not a live research dashboard. The issue owns current status, implementation ownership, and subsequent validation. Findings describe the audited source, not an assertion about any later `main` revision.

## Read the audit

- [Complete original report](REPORT.md): exact source and specification references, minimized witnesses, independent checks, negative results, architectural representation analysis, other-family boundary designs, coverage denominator, reproduction, and licensing.
- [Complete original candidate fix and C regression patch](candidate-fix-and-regression.patch): retained as an **unapplied, unrun candidate**, not a production source change.
- [Machine-readable original coverage ledger](coverage.json): separates executed reference checks, deterministic source deductions, authored-but-unrun tests, catalog populations, and unverified work.
- [Complete evidence archive: all 23 original files](buster-encoding-audit-f34cc3a.tar.xz).
- [Original member checksum manifest](package.SHA256SUMS) and [publication integrity verification](PUBLICATION-VERIFICATION.txt).

The archive contains every file from the original chat-delivered `buster-encoding-audit-f34cc3a.zip`. It has been repackaged as tar.xz with byte-identical member contents; the compressed container and its metadata are different. The original ZIP itself is not the published tar.xz and their hashes must not be confused. The standalone report, patch, and coverage ledger duplicate their original archive members for convenient browsing.

## What was established

One source-confirmed defect family: the existing A64 ADD/ADDS/SUB/SUBS extended-register encoder and typed decoder reject legal 32-bit UXTX/SXTX combinations. The old boundary test repeats the same incorrect expectation. The minimal words are `0x0b226020` and `0x0b22e020`.

The original cloud reference experiment checked 960 legal words with zero mismatches and 192 reserved raw words that all decoded as unknown. The negative source driver **failed its initial all-rejected expectation**: 252 of 256 spellings were rejected. Four LLVM source-width spellings were permissively accepted and decoded with Xm rather than Wm. Every diagnostic and these discrepancies are retained. The assembler and disassembler are correlated LLVM implementations; independent ISA-derived field arithmetic is the separate expected-word check.

The 2,600 authored C regression inputs are **NOT RUN against Buster**. The Buster build, existing harness, candidate patch applicability, complete source/target/canonical routes, production codegen reachability, and generated AArch64 execution remain unverified in this audit. `git apply --numstat` was a patch syntax check only. The x86 work is a representation inventory, not a completed prefix/address/encoding audit. Other A64 boundary families are test designs, not passing tests.

## Complete archive inventory

All paths below are under `buster-encoding-audit-f34cc3a/` after extraction:

```text
README.md
REPORT.md
coverage.json
candidate-fix-and-regression.patch
buster_extension_regression.c
extension_oracle.c
extension_negative_oracle.c
extension-family.s
extension-family.disassembly.txt
extension-oracle-result.txt
extension-negative-cases.tsv
extension-negative-diagnostics.txt
extension-negative-result.txt
extension-reserved.s
extension-reserved.disassembly.txt
extension-reserved-result.txt
reference-syntax-permissiveness.s
reference-syntax-permissiveness.disassembly.txt
a64-add-extend.s
tool-versions.txt
source-excerpts.txt
patch-syntax-check.txt
SHA256SUMS
```

`buster_extension_regression.c` is an insertion block for the existing test body, not a standalone translation unit. `extension_oracle.c` and `extension_negative_oracle.c` are the independently written reference drivers. Captured disassemblies and diagnostics are historical outputs, not results of rerunning anything during publication.

The original inner README and coverage ledger say the candidate was not committed. They are preserved verbatim as statements of the original audit state. This evidence branch stores the patch as an artifact only; it does not apply that patch to the production or test sources.

## Integrity and recovery

Original chat ZIP SHA-256:

```text
8e182a4573b3b390708c28c95608eda5cbdb3740b6039a077f3cd8c618ab2150
```

Published tar.xz SHA-256:

```text
9be39ee3b3c13f893f8afcac51ea1ae1f50fab3e70508f4ba6b6803da4f27a55
```

Published tar.xz Git blob SHA:

```text
06aba7ff59d6d7ef4e812a55d031bd41cafa2fed
```

After obtaining the archive from GitHub, verify and extract it with existing tools:

```sh
printf '%s  %s\n' \
  9be39ee3b3c13f893f8afcac51ea1ae1f50fab3e70508f4ba6b6803da4f27a55 \
  buster-encoding-audit-f34cc3a.tar.xz | sha256sum -c -
tar -xJf buster-encoding-audit-f34cc3a.tar.xz
cd buster-encoding-audit-f34cc3a
sha256sum -c SHA256SUMS
```

All 23 member contents were compared byte-for-byte with the original ZIP. The original manifest lists the other 22 members, and all 22 hashes passed. The original `SHA256SUMS` file itself has SHA-256 `e4f5f6553d1db546196a720276eabc070ba8c565f2a83fb329813bba93050e1b`. The standalone report and candidate patch were also checked against their original Git blob identities after upload.

These integrity checks establish faithful publication, not compiler correctness.

## Licensing and closure boundary

At the audited revision, Buster's `THIRD_PARTY_NOTICES.md` does not select a first-party project license. It separately records Intel XED as Apache-2.0, LLVM-derived material as Apache-2.0 WITH LLVM-exception, and unresolved Arm-derived redistribution review. The actually used LLVM reference tool's license was checked at its reported source revision; see report section 9 for the pinned license URL and blob. Publishing these artifacts neither relicenses Buster nor resolves that review. No Arm XML, upstream encoder implementation, executables, object files, or new repository dependency is included.

No production fix, PR, merge, workflow change, or new test execution is claimed by this publication. The immediate technical gate remains reproduction through the existing Buster C harness on an authorized cloud/GitHub-hosted machine; no laptop or physical Ryzen 9700X is authorized for that audit. The issue remains the place to record an accepted next owner and actual validation results.
