# Assembly metadata

This directory contains deterministic, pointer-free assembly metadata emitted
by `build import_assembly_metadata`. Normal builds do not parse JSON, execute
XED/LLVM, or run TableGen. The generated x86-64 C header is consumed by the
x86-64 metadata ABI and bounded lookup layer, while the current assembler
encoder does not yet consume its encoding fields. The AArch64 tables remain an
audit artifact: the acceptance importer fails closed while any record lacks an
exact proven schema, and the current assembler runtime does not consume them
yet.

- `x86_64-xed.jsonl`, `x86_64-assembly.generated.h`, and
  `x86_64-coverage.generated.inc` are the existing checked-in XED artifacts.
- `x86_64-assembly.generated.h` is a 6,092,260-byte compact ABI artifact:
  11,013 forms, 32,813 operands, a 1,726,254-byte logical string pool, and
  immutable little-endian packed blobs accessed through bounded generated
  accessors. Flat C string chunks are at most 4,092 payload bytes, avoiding a
  nested initializer or runtime table construction in self-hosted builds.
  Generated sorted indexes contain 1,942 mnemonic ranges/11,019 candidates,
  1,995 iclass ranges/11,013 candidates, 5,855 iform ranges/7,525 candidates,
  and form/coverage hash indexes with 11,013 ranges and candidates each.
  Mnemonics are ASCII-case-insensitive source spellings from the first token of
  Intel, AT&T, and generic disassembly fields; iclass and iform remain separate
  exact diagnostic indexes. Numeric form IDs are snapshot row IDs; `stable_hash`
  is the durable form identity.
- `x86_64-coverage.generated.inc` is a 392,002-byte packed coverage include.
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
  bytes, with a bounded non-inline switch accessor; there are no nested
  aggregates, pointer tables, or runtime initialization paths, so the
  pointer-free source remains consumable by Buster.
- `aarch64-coverage.generated.inc` contains one stable row for every input
  record, including explicit source and name hashes, normalized form ID,
  classification, encoder family, test class, and reason ID.
- `aarch64-missing-fields.generated.jsonl` is the exact machine-readable audit
  inventory for every RESERVED/UNENCODABLE or UNSUPPORTED_TOKEN row.
- `manifest.json` records provenance, checksums, sizes, counts, table totals,
  lookup totals, alias boundaries, and the acceptance status.

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
22,631 fields, 23,037 segments, 26,262 operands, 7,854 predicate uses, and
116 distinct predicate features. The flat-chunk header is 3,974,975 bytes
(checksum `378dfee753bda56b`), the flat-chunk coverage include is 301,471
bytes (checksum `a10e73399b8a8b75`), and the sorted string pool is 337,490
bytes. Lookup
indexes contain 1,557 mnemonic ranges and candidates for all 7,491 records;
the proven-signature index contains 4,310 ranges and 4,310 candidates.

Coverage is intentionally blocked:

```text
DIRECT=37 NORMALIZED=4250 ALIAS=0 PRIVILEGED/SYSTEM=23
RESERVED/UNENCODABLE=147 UNSUPPORTED_TOKEN=3034 UNCLASSIFIED=0
```

The reason totals are:

```text
NONE=4287 SYSTEM_OR_PRIVILEGED=23 UNMAPPED_VARIABLE=147 NULL_FIELD=24
UNPROVEN_FIELD_SEMANTICS=1018 UNPROVEN_OPERAND_KIND=1298
UNPROVEN_IMMEDIATE_RANGE=486 UNPROVEN_MEMORY_FORM=144
UNPROVEN_TIED_OPERAND=32 UNPROVEN_CORRESPONDENCE=32
```

The inventory contains 3,181 rows, is 581,211 bytes, and has checksum
`951f5fb99507e0ce`. Every row carries the source hash, name hash, normalized
form ID, classification, family, test class, and exact reason ID. The
acceptance command fails with these rows present; `--audit` is the deliberate
report mode and does not certify completeness.

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
artifact, including `aarch64-missing-fields.generated.jsonl` and `manifest.json`.
The importer also verifies that the copied XED artifacts have their expected
bytes, checksums, line count, and generated-count macros; a one-byte mutation
fails provenance validation. The current local audit generation takes about
3.3 seconds including the `build.c` bootstrap on the reference Linux x86-64
workspace. A strict include probe should exercise the generated header with
Buster itself before compiler-matrix checks; the flat chunk representation is
chosen to avoid exhausting Buster's C IR fallback arena.

The generated AArch64 header is not included in the Release unity translation
unit yet, so the current Release unity compile impact is zero bytes of
compiled-source input. Future runtime integration must measure its own lookup
and emitter impact.

## Runtime boundary

This tranche supplies metadata and objective coverage only. Remaining runtime
work is to add bounded mnemonic/signature candidate selection, validate source
operands against the exact descriptor grammar, implement register/immediate,
memory/list/lane/system and relocation transforms, connect feature predicates,
and emit AArch64 machine code through the assembler front door. Alias import
and canonical linking must be completed before the full raw source can be
accepted. The checked-in DIRECT rows are only fixed-mask/no-operand forms
proven by the current schema; no runtime encoding claim is made for the
blocked normalized inventory.

XED data is Apache-2.0. LLVM data is Apache-2.0 WITH LLVM-exception. Exact
revisions, checksums, and generated sizes are in `manifest.json`.
