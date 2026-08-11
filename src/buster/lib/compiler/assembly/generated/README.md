# Assembly metadata

This directory contains deterministic, pointer-free assembly metadata emitted
by `build import_assembly_metadata`. Normal builds do not parse JSON, execute
XED/LLVM, or run TableGen. The generated x86-64 C header is consumed by the
x86-64 metadata ABI and bounded lookup layer, while the current assembler
encoder does not yet consume its encoding fields. The AArch64 packed tables are
consumed by the metadata runtime and raw bit-layout encoder; semantic coverage
and the Apple M1 profile remain provisional until raw LLVM aliases and the
official Arm XML are independently validated. The profile is emitted as a
separate, deterministic JSONL projection over the pinned AArch64 records.

- `x86_64-xed.jsonl`, `x86_64-assembly.generated.h`, and
  `x86_64-coverage.generated.inc` are the existing checked-in XED artifacts.
- `x86_64-assembly.generated.h` is a 6,052,140-byte compact ABI artifact:
  11,013 forms, 32,813 operands, a 1,726,254-byte logical string pool, and
  immutable little-endian packed blobs accessed through bounded generated
  accessors. Flat C string chunks are at most 4,092 payload bytes, avoiding a
  nested initializer or runtime table construction in self-hosted builds;
  each chunked blob also carries constant chunk pointer/length tables so the
  bounded accessors index one flat table instead of dispatching a
  several-hundred-way switch per byte.
  Generated sorted indexes contain 1,942 mnemonic ranges/11,019 candidates,
  1,995 iclass ranges/11,013 candidates, 5,855 iform ranges/7,525 candidates,
  and form/coverage hash indexes with 11,013 ranges and candidates each.
  Mnemonics are ASCII-case-insensitive source spellings from the first token of
  Intel, AT&T, and generic disassembly fields; iclass and iform remain separate
  exact diagnostic indexes. Numeric form IDs are snapshot row IDs; `stable_hash`
  is the durable form identity.
- `x86_64-coverage.generated.inc` is a 389,599-byte packed coverage include.
  It has one checked row per form and reports DIRECT=0, NORMALIZED=10,636,
  NOT64=268, PRIVILEGED=109, RESERVED=0, UNSUPPORTED_TOKEN=0, and
  UNCLASSIFIED=0. NORMALIZED rows are metadata coverage, not a claim that the
  runtime assembler can encode them.
- `aarch64-llvm.jsonl` contains one reduced non-pseudo `AArch64Inst` record per
  line.
- `aarch64-assembly.generated.h` contains compact fixed masks/values,
  bit-sliced variable fields and segments, relocation metadata, syntax-neutral
  operands, address descriptors, ties, predicates, sorted strings, and lookup
  indexes. Numeric tables use deterministic packed base64 byte blobs with
  fixed little-endian wire layouts and generated typed accessors. Each blob is
  emitted as independent flat C string chunks no larger than 4,092 payload
  bytes, with constant chunk pointer/length tables and bounded non-inline
  accessors; there are no runtime initialization paths, and the constant
  symbol-address tables lower through the same static-initializer relocation
  path Buster already supports, so the source remains consumable by Buster.
- `aarch64-form-ids.generated.h` contains the named snapshot IDs for all 44
  forms used by the machine AArch64 backend (the eight scalar unsigned
  load/store forms plus the scalar, register, SP, RET, and FMOV families). It
  is generated from source names during import so production code never embeds
  row numbers by hand. The current artifact is 2,892 bytes (checksum
  `21bb44e355ee0e82`), and its checksum/size are recorded in `manifest.json`.
- `aarch64-production-plan.generated.h` is the direct, predecoded production
  plan consumed by the machine encoder. It contains 44 forms, 149 source
  fields, and 163 bit segments; its current size is 104,303 bytes (checksum
  `5cfdff7016801587`). `aarch64-production-plan.generated.jsonl` is the
  deterministic, human-readable projection of the same plan (19,486 bytes,
  checksum `b2305bba7fc58c35`). Both artifact checksums, sizes, and counts are
  recorded in `manifest.json` and are regenerated from the same normalized
  source rows.
- `aarch64-coverage.generated.inc` contains one stable row for every input
  record, including explicit source and name hashes, normalized form ID,
  classification, encoder family, test class, and reason ID.
- `aarch64-missing-fields.generated.jsonl` is the exact machine-readable audit
  inventory for every RESERVED/UNENCODABLE or UNSUPPORTED_TOKEN row.
- `aarch64-apple-m1-profile.generated.jsonl` is the self-describing Apple M1
  profile projection. Every source row is retained with an explicit
  in-profile/excluded decision, stable source/name hashes, normalized form ID,
  classification, and the predicates that caused an exclusion.
- `manifest.json` records provenance, checksums, sizes, counts, table totals,
  lookup totals, alias boundaries, the M1 predicate policy, and acceptance
  status.

### Official Arm A-profile system-register inventory

`aarch64-system-registers.generated.jsonl`,
`aarch64-system-registers.generated.h`, and
`aarch64-system-registers-manifest.json` are generated from the official Arm
A-profile SysReg XML 2026-06 release. The 1,400 relevant mechanisms are the
`MRS`, `MSRregister`, `MRRS`, and `MSRRregister` SystemAccessor pages. The
schema-2 JSONL is a compact normalized inventory: every row retains source,
feature, and access-permission digests, mechanism/mode, packed layout, array
range, and a numeric decision reason, while raw XML condition/accessor prose
is intentionally omitted. The pointer-free C header retains only accepted
runtime identity strings and precomputed parameter transforms. ASL privilege
and trap text is provenance, never assembler legality. The audited
hardware-grounded Apple M1 A-profile accepts 402 mechanisms: 392 fixed named
rows, 8 named parameterized rows, and 2 mechanisms (MRS/MSRregister) in one
generic S3 family (10 parameterized mechanisms total). Fixed rows cover 202
target names/201 encodings (200 readable, 138 writable, 136 both). Generic S3
remains available through the bounded raw parser and is excluded from named
lookup.

The profile evaluator consumes both Boolean branches, honors
parentheses/precedence/NOT, combines register and access predicates, and fails
closed for unknown feature/value atoms. The exact TRUE atoms are
`FEAT_AA64`, `EL1`, `EL2`, `FEAT_VHE`, `FEAT_AES`, `FEAT_AdvSIMD`, `FEAT_CRC32`,
`FEAT_DIT`, `FEAT_DotProd`, `FEAT_FCMA`, `FEAT_FHM`, `FEAT_FP`, `FEAT_FP16`,
`FEAT_FRINTTS`, `FEAT_FlagM`, `FEAT_FlagM2`, `FEAT_JSCVT`, `FEAT_LOR`,
`FEAT_LRCPC`, `FEAT_LRCPC2`, `FEAT_LSE`, `FEAT_PAuth`, `FEAT_RDM`, `FEAT_SB`,
`FEAT_SHA1`, `FEAT_SHA256`, `FEAT_SHA3`, `FEAT_SHA512`, `FEAT_SPECRES`,
`FEAT_SSBS`, `FEAT_RAS`, `FEAT_PAN`, `FEAT_UAO`, `FEAT_TLBIOS`,
`FEAT_TLBIRANGE`, and `FEAT_DPB`. EL3, AMUv1, MPAM, SEL2, NV/NV2, TRF,
PMUv3, RASv1p1, D128/SRMASK/SYSREG128, trace access, and unknown atoms are
explicitly excluded. The access-condition audit records the exact 34-row
removal from the register-only census: D128=20, SRMASK=8, SYSREG128=2, and
unknown atoms=4. FEAT_VHE access rows remain eligible (72 accepted); runtime
`access_permission` ASL is retained only as a digest and never determines
assembler legality. MRRS/MSRR pair helpers remain available as explicit D128
layout utilities, but no pair row is in this Apple profile.

### Hardware evidence and scope

The profile is grounded in a [community 2022 M1 `cpuctl` sample](https://gist.github.com/ryo/f533af313ac9dfd971f682b7ae951d63)
covering both Icestorm and Firestorm cores; it is not Apple-primary evidence and
is not exhaustive across every M1 revision. The [m1n1 hypervisor source at the
corroborating commit](https://github.com/AsahiLinux/m1n1/blob/06a4601a351ebfd1abb6abba9a44c34e40d94776/src/hv.c)
corroborates the EL1/EL2 and VHE baseline. The [Asahi Linux Apple M1 PMU
driver](https://github.com/AsahiLinux/linux/blob/e2e1930a9595bffafad92cec2b5504525efb9cd4/drivers/perf/apple_m1_cpu_pmu.c)
is non-architectural platform-software evidence only; it does not prove
architectural FEAT_PMUv3, so PMUv3 remains excluded.

The source archive is the 30 Jun 2026 release (`2026-06_rel`), URL
`https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-Arm-Architecture-System-Registers/SysReg/SysReg_xml_A_profile-2026-06.tar.gz`,
SHA-256 `4795c769085ff9056d9f18abbd9e23d7b0f0a955214cfb2a2121a9698b50d509`
and 43,124,482 bytes. Arm's notice/provenance digest is
`13a5c90f2accaf17573f73499a3940df6168d65cd17a893f685e686aa436a246`.
The proprietary archive/XML is not vendored. The importer requires the exact
release inventory of 1,943 XML files and 1,400 relevant mechanisms; the
manifest records both expected and observed counts. To regenerate,
independently obtain the official archive, extract it, and run the bounded
sysreg importer; `--check` compares deterministic output and rejects malformed
or truncated XML rather than accepting partial data.

## Pinned snapshot and current status

The LLVM source is `llvmorg-22.1.8`, commit
`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`. The checked-in reduced JSONL has
7,491 records and checksum `f2e553abd71696e5`. A reduced-input checksum is not
the raw TableGen snapshot checksum; the AArch64-only importer records
`raw_snapshot_provenance=false` and never attaches the raw checksum to an
arbitrary input. The raw checksum `4c3cec3a88d0c821` is accepted only by the
full importer after exact count and checksum validation.

The full dual-input manifest keeps provenance namespaces distinct:
`xed.input_kind` is `xed_enc_instructions` and is marked unverified unless the
local XED checkout identity is independently established; LLVM uses
`llvm_tblgen_json` with its own source identity and raw-snapshot flag. The
reduced-input manifest records the raw reduced checksum separately from the
checksum of the normalized JSONL emitted by the importer.

The current audit output has 7,491 coverage rows, 7,491 canonical forms,
22,631 fields, 23,039 segments, 26,262 operands, 7,855 predicate uses, and
116 distinct predicate features. The flat-chunk header is 3,952,180 bytes
(checksum `52b6075d82359ef6`), the flat-chunk coverage include is 299,898
bytes (checksum `ec5065a9b4503e40`), and the sorted string pool is 337,490
bytes. Lookup
indexes contain 1,557 mnemonic ranges and candidates for all 7,491 records;
the proven-signature index contains 4,333 ranges and 4,333 candidates.

Coverage is intentionally blocked:

```text
DIRECT=37 NORMALIZED=4271 ALIAS=0 PRIVILEGED/SYSTEM=25
RESERVED/UNENCODABLE=145 UNSUPPORTED_TOKEN=3013 UNCLASSIFIED=0
```

The reason totals are:

```text
NONE=4308 SYSTEM_OR_PRIVILEGED=25 UNMAPPED_VARIABLE=145 NULL_FIELD=0
UNPROVEN_FIELD_SEMANTICS=1018 UNPROVEN_OPERAND_KIND=1298
UNPROVEN_IMMEDIATE_RANGE=489 UNPROVEN_MEMORY_FORM=144
UNPROVEN_TIED_OPERAND=32 UNPROVEN_CORRESPONDENCE=32
```

The inventory contains 3,158 rows, is 577,108 bytes, and has checksum
`2b59a0e89f2303af`. Every row carries the source hash, name hash, normalized
form ID, classification, family, test class, and exact reason ID. The
acceptance command fails with these rows present; `--audit` is the deliberate
report mode and does not certify completeness.

### Apple M1 profile

The checked-in Apple M1 projection is a provisional, LLVM-derived profile. Its
predicate policy is the exact 24-feature closure from the pinned LLVM
AppleA14/HasV8_4aOps definition: HasAES, HasAltNZCV, HasCRC, HasComplxNum,
HasDotProd, HasEL3, HasFP16FML, HasFPARMv8, HasFRInt3264, HasFlagM,
HasFullFP16, HasJS, HasLOR, HasLSE, HasNEON, HasNEONandIsStreamingSafe,
HasPAuth, HasRCPC, HasRCPC_IMMO, HasRDM, HasSB, HasSHA2, HasSHA3, and
HasTRACEV8_4 (the FEAT_TRF closure). HasLOR and FEAT_TRF are intentional
AppleA14 additions: the independent closure audit attributes eight non-system
forms to FEAT_LOR and one system TSB form to FEAT_TRF. An empty predicate list
is the baseline; unknown predicates and any predicate outside this set are
explicit exclusions, never silent support.

For the 7,491 checked-in LLVM rows this produces 2,898 provisional in-profile
rows and 4,593 explicit exclusions (4,962 excluded predicate occurrences).
The in-profile classification counts are
DIRECT=22, NORMALIZED=1,942, PRIVILEGED/SYSTEM=18,
RESERVED/UNENCODABLE=0, UNSUPPORTED_TOKEN=916, with ALIAS=0 and
UNCLASSIFIED=0. The exclusion counts are DIRECT=15, NORMALIZED=2,329,
PRIVILEGED/SYSTEM=7, RESERVED/UNENCODABLE=145, UNSUPPORTED_TOKEN=2,097,
with ALIAS=0 and UNCLASSIFIED=0. The profile acceptance gate is therefore
blocked by 916 in-profile unsupported rows; non-M1 extensions (for example
SVE/SVE2, SME/SME2, MTE, BF16, I8MM, and BTI) remain explicit exclusions and
do not inflate that denominator. `MSRpstatesvcrImm1` is excluded by the
explicit `SVCROperand` SME custom-parser gate despite its empty LLVM
`Predicates` list; its deterministic profile exclusion reason is
`custom_operand_requires_sme`; the importer materializes that hidden parser
requirement as a `HasSME` predicate for every target, and the form remains
raw-layout complete. Raw layout
closure is complete for all 2,898 in-profile rows (2,898/2,898); this is a
bit-layout guarantee, not semantic encoder coverage. The emitted profile is
2,537,289 bytes with checksum `a16e675c38fe059f`.

The MRS/MSR system forms with immediate operands remain in the M1 denominator
when their feature predicates permit them. Their operand-value semantics are
not yet validated by the raw-layout closure and remain a separate runtime
validation task.

The HasRCPC_IMMO member is intentional: the pinned source tags the
LDAPUR*/STLUR* unscaled-immediate forms with HasRCPC_IMMO. Dropping it would
misclassify Armv8.4-A load-acquire/store-release forms that are part of the M1
closure. This predicate-derived count is not the independent Arm XML count.
The official Arm A64 ISA audit selected 1,695 of 4,623 candidate rows
(1,523 canonical and 172 aliases), or 1,653 rows after excluding system
instructions (1,490 canonical and 163 aliases). Those XML figures are
cross-check evidence only; the proprietary raw XML is not vendored and is not
used to claim that the LLVM profile is complete.

<!-- arm-a64-canonical-check:start -->
### Official Arm A64 canonical inventory

`arm-a64-canonical.generated.jsonl` and `arm-a64-canonical-manifest.json` are
schema-2 outputs generated by the bounded importer in
`src/buster/lib/compiler/assembly/arm_a64_canonical_import.c`. The importer
enumerates every `instructionsection` page in the pinned Arm A64 XML release,
not only the 516-page convenience index, and retains one row per encoding with
fixed bits, split fields, unresolved constraints, aliases, assembly templates,
functional equivalent-template text and alias conditions with source markup
and descriptive attributes removed,
page-level instruction class, named box/cell constraints, and the original
`arch_variant` predicate. Every row starts with `schema_version: 2`; hex bit
masks and values are JSON strings (for example `"0xff800000"`) so the JSONL
remains standards-compliant while preserving the canonical spelling. Alias rows
retain a separately sanitized target `encoding_id`, are checked to resolve to
exactly one canonical row, and carry the canonical page's ordered
`alias_preferences` (source file, page ID, condition, and rank).
`alias_preference_condition` and `alias_preference_rank` are copied onto each
alias row, so a decoder can first resolve the canonical encoding identity and
then apply the source-prescribed alias preference pass.

The release archive is `ISA_A64_xml_A_profile-2026-06.tar.gz` with SHA-256
`63a01a1696483bbe2edfef9e0f0cd053d6c1c619ec0587876cb7a60bb344f354`. The raw
XML remains outside the repository under Arm's distribution terms; the
manifest records the stable source URL, externally pinned archive checksum, and
aggregate extracted-tree hashes without embedding a machine-local directory.
The importer does not read archive bytes and therefore marks the archive
identity as externally pinned rather than locally verified. Its in-process
SHA-256 covers the exact sorted top-level regular-file stream (filename, NUL,
little-endian byte length, bytes), while the recorded FNV-1a-64 value is a
convenience cross-check. The pinned snapshot has 2,292 pages (2,121
instruction, 170 alias, and 1 pseudocode),
3,502 iclasses/regdiagrams, and 4,623 encodings. The importer verifies the
complete 2,316-file top-level regular-input set (2,299 XML plus DTD/XSL/CSS/SVG
support files; nested `xhtml/` and diff presentation trees are excluded) using
SHA-256 `0ee17fd2fe7ed165adda377d90f8f284d009e14d2300577f231c87ca6a45916d`
and FNV-1a-64 `0xba0c8fc560297896`. Its Apple-A14/M1 Boolean closure
selects 1,695 rows (1,523 canonical and 172 aliases), including 42 page-level
system rows; excluding system rows leaves 1,653 (1,490 canonical and 163
aliases). Acceptance remains blocked pending semantic operand and
system-register dictionaries plus LLVM cross-references.

The importer resolves the class `regdiagram` first and applies each encoding
as a sparse overlay. Binary cells are authoritative; empty overlay cells
inherit the base bit (with a named empty cell retaining its source field),
named symbolic cells are promoted to variable fields only for canonical
Apple-M1 rows, and unnamed symbolic cells remain unresolved. `!=` box
constraints are retained as constraints while their `colspan` positions are
tracked one source bit at a time. This profile-scoped promotion removes the
unresolved and explicit-unresolved masks from all 1,523 Apple-M1 canonical
rows. Exactly 133 rows change, while the 22 M1 aliases, 129 non-M1 canonical
rows, and 3 non-M1 aliases with source uncertainty retain their masks. The
sorted `id + space + old_mask` identity digest is
`c6062407c284feb7746a91214c67a739dcb70c8b763962ae1ff353036a231543`.

The symmetric-difference gate is exact: all 17 PAuth-LR rows in the exclusion
set must be `apple_m1: false`, and all 16 Armv8.5/crypto/special rows in the
included set must be `apple_m1: true`; names missing from either set or present
with the wrong decision fail the import. The manifest's `schema_version` and
`artifact_schema_version` are both 2. Consumers must not treat schema-1 rows as
interchangeable because schema 2 adds alias identities/preferences, box
constraints, and explicit mask semantics.

The same importer emits `arm-a64-m1-fixed.generated.h`, a deterministic
34-row spelling-to-word table for Apple-M1 rows whose Arm masks are fully
fixed (`fixed_mask == 0xffffffff`, with no field or unresolved bits). It keeps
32 canonical rows and the two `PSSBB`/`SSBB` aliases, including each Arm row
ID, digest, and required target feature for provenance and explicit feature
subtraction. The manifest records the fixed header's byte count and XXH64 as
well as the canonical JSONL identity.

It also emits `arm-a64-m1-gpr.generated.h`, a compact direct-register
projection of the canonical Arm XML. The projection is structurally selected
from non-system Apple-M1 canonical rows whose templates contain only one to
four scalar W/X registers (with an optional `|SP` role for register 31), whose
fields are one contiguous five-bit segment, and whose masks cover all 32 bits.
The checked-in census is 80 forms, 63 mnemonics, arities 18/23/31/8, and
feature counts baseline/CRC32/FlagM/PAuth 43/8/2/27. Generated rows preserve
the visible operand order, fixed mask/value, required target feature, Arm row
ID, and a deterministic source digest; the runtime encoder does not depend on
LLVM packed metadata or oracle words. The manifest records this header's
bytes/XXH64 and semantic census.

It also emits `arm-a64-m1-scalar-integer.generated.h`, a 72-form structural
projection of the non-system Apple-M1 scalar-integer families. The census is 23
mnemonics, arities 1/2/3/4 = 1/6/49/16, baseline/FlagM features 71/1, and
recipe counts add/sub-ext 8, add/sub-imm 8, add/sub-shift 8, logical-imm 8,
logical-shift 16, bitfield 6, extract 2, movewide 6, conditional-compare
immediate/register 4/4, RMIF 1, and UDF 1. The normalized source identity is
SHA-256 `4429d9ab064a8e98561c794e8c5408a3922bc6a7f07a32015caaa9932ba2c484`;
all selected rows have an unresolved mask of zero. Regeneration writes seven
Arm artifacts to the requested output directory.

The canonical decoder snapshot is emitted as
`aarch64-canonical-decoder.generated.h` with its deterministic audit in
`aarch64-canonical-decoder-audit.json`. It contains exactly 1,523 canonical
Apple-M1 rows (aliases are excluded), with complete fixed/variable bit
partitions, 5,387 packed fields, 10,767 source segments, 215 deduplicated
constraint programs (707 postfix tokens), and 24 target-feature programs.
`tools/gen_aarch64_canonical_decoder.py` is the optional developer-side
regenerator; normal imports remain zero-dependency and consume only the
repository-pinned snapshot. The C importer independently checks the canonical
row count, source digest, artifact sizes/checksums, and decoder invariants. The
bounded runtime validates fixed bits and constraints, filters
by target features, and resolves overlapping encodings by fixed-mask
specificity; its pointer-free API exposes raw encode/decode and explicit
success, unallocated, unsupported-feature, ambiguous, and incomplete statuses.
The audit records deterministic representative words and the collision census:
22 constrained representative pairs, including the 23-row generic HINT group.
The importer copies and verifies both decoder snapshots, and records their
sizes/checksums in `arm-a64-canonical-manifest.json`.

Regenerate into a separate directory and compare all seven output files byte-for-
byte across two runs:

```sh
./build.sh import_arm_a64_metadata \
  /path/to/ISA_A64_xml_A_profile-2026-06 \
  /tmp/arm-a64-generated
```

The importer rejects source-tree mutations before emitting artifacts and
rejects duplicate canonical IDs/digests or unresolved alias targets. Run the
command twice into separate directories and byte-compare all seven generated
files (the SHA-256/FNV source identity, SHA-256 `abc` known vector, bounded
feature-parser tests, direct-GPR/scalar-integer structural censuses, and symmetric-difference
gate provide in-process verification).

For a checked-in drift audit, point the importer at the generated directory
with `BUSTER_ARM_A64_CHECK=1`. This performs the full parse and compares the
would-be JSONL, manifest, fixed-spelling, direct-GPR, scalar-integer,
canonical-decoder header, and canonical-decoder audit bytes plus the bounded
canonical README section SHA-256, then exits without writing anything.
Unrelated README sections may be edited without changing importer source:

```sh
BUSTER_ARM_A64_CHECK=1 ./build.sh import_arm_a64_metadata \
  /path/to/ISA_A64_xml_A_profile-2026-06 \
  src/buster/lib/compiler/assembly/generated
```
<!-- arm-a64-canonical-check:end -->

## Regeneration

For a full pinned import, obtain the matching local XED datafiles directory and
LLVM TableGen JSON, then run:

```sh
./build.sh import_assembly_metadata \
  /path/to/xed/datafiles \
  /path/to/AArch64.json \
  /tmp/aarch64-generated
```

The command succeeds only when the raw LLVM provenance is exact, no aliases
are omitted, and every AArch64 row has a proven schema. For local work without
those checkouts, generate the checked-in audit artifact with:

```sh
./build.sh import_assembly_metadata --audit \
  - src/buster/lib/compiler/assembly/generated/aarch64-llvm.jsonl \
  /tmp/aarch64-generated
```

Run the audit command twice into separate directories and byte-compare every
artifact, including `aarch64-apple-m1-profile.generated.jsonl`,
`aarch64-missing-fields.generated.jsonl`, and `manifest.json`.
The importer also verifies that the copied XED artifacts have their expected
bytes, checksums, line count, and generated-count macros; a one-byte mutation
fails provenance validation. The current local audit generation takes about
3.3 seconds including the `build.c` bootstrap on the reference Linux x86-64
workspace. A strict include probe should exercise the generated header with
Buster itself before compiler-matrix checks; the flat chunk representation is
chosen to avoid exhausting Buster's C IR fallback arena.

The generated AArch64 header is included by the metadata runtime. Future
assembler-front-door integration must measure lookup and emitter impact.

## Runtime boundary

This tranche supplies the packed metadata ABI, bounded lookup, raw fixed-bit
validation, and objective coverage. Remaining runtime work is to add complete
mnemonic/signature candidate selection, validate source operands against the
descriptor grammar, implement register/immediate, memory/list/lane/system and
relocation transforms, connect feature predicates, and emit AArch64 machine
code through the assembler front door. Alias import and canonical linking must
be completed before the full raw source can be accepted. Semantic encoding is
still not claimed for the blocked normalized inventory or for the provisional
M1 profile.

XED data is Apache-2.0. LLVM data is Apache-2.0 WITH LLVM-exception. Exact
revisions, checksums, and generated sizes are in `manifest.json`.
