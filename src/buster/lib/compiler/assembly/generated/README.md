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
- `aarch64-form-ids.generated.h` contains the named snapshot IDs for the
  scalar unsigned load/store forms used by the machine AArch64 backend. It is
  generated from source names during import so production code never embeds
  row numbers by hand. The current artifact is 730 bytes (checksum
  `58f6813468469913`), and its checksum/size are recorded in `manifest.json`.
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
22,631 fields, 23,039 segments, 26,262 operands, 7,854 predicate uses, and
116 distinct predicate features. The flat-chunk header is 3,951,958 bytes
(checksum `21ff206904cdbd7a`), the flat-chunk coverage include is 299,898
bytes (checksum `ec5065a9b4503e40`), and the sorted string pool is 337,490
bytes. Lookup
indexes contain 1,557 mnemonic ranges and candidates for all 7,491 records;
the proven-signature index contains 4,310 ranges and 4,310 candidates.

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

For the 7,491 checked-in LLVM rows this produces 2,899 provisional in-profile
rows and 4,592 explicit exclusions (4,961 excluded predicate occurrences).
The in-profile classification counts are
DIRECT=22, NORMALIZED=1,942, PRIVILEGED/SYSTEM=19,
RESERVED/UNENCODABLE=0, UNSUPPORTED_TOKEN=916, with ALIAS=0 and
UNCLASSIFIED=0. The exclusion counts are DIRECT=15, NORMALIZED=2,329,
PRIVILEGED/SYSTEM=6, RESERVED/UNENCODABLE=145, UNSUPPORTED_TOKEN=2,097,
with ALIAS=0 and UNCLASSIFIED=0. The profile acceptance gate is therefore
blocked by 916 in-profile unsupported rows; non-M1 extensions (for example
SVE/SVE2, SME/SME2, MTE, BF16, I8MM, and BTI) remain explicit exclusions and
do not inflate that denominator. Raw layout closure is complete for all 2,899
in-profile rows (2,899/2,899); this is a bit-layout guarantee, not semantic
encoder coverage. The emitted profile is 2,537,249 bytes with checksum
`ad73d60fbca76b9c`.

The HasRCPC_IMMO member is intentional: the pinned source tags the
LDAPUR*/STLUR* unscaled-immediate forms with HasRCPC_IMMO. Dropping it would
misclassify Armv8.4-A load-acquire/store-release forms that are part of the M1
closure. This predicate-derived count is not the independent Arm XML count.
The official Arm A64 ISA audit selected 1,695 of 4,623 candidate rows
(1,523 canonical and 172 aliases), or 1,653 rows after excluding system
instructions (1,490 canonical and 163 aliases). Those XML figures are
cross-check evidence only; the proprietary raw XML is not vendored and is not
used to claim that the LLVM profile is complete.

The reduced source does not contain an `AArch64InstAlias` class, so its alias
count is honestly zero and no alias preservation is claimed. The full raw
path detects an alias class if present and blocks because separate alias
record import/canonical linking is not implemented in this tranche.

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
