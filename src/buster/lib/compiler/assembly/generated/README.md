# Assembly metadata

This directory contains the checked-in, deterministic assembly metadata used
by the assembler work. Normal builds do not parse JSON, execute XED/LLVM, or
depend on either checkout. The generated C header is intentionally not
included by the current assembler runtime yet; the follow-on encoder tranche
will consume it.

- `x86_64-xed.jsonl` is the canonical audit representation: one complete raw
  XED `PATTERN`/`OPERANDS` form per line, including source provenance.
- `x86_64-assembly.generated.h` is the pointer-free compact C schema and
  tables: sorted string pool, operands, normalized forms, encoding fields,
  decorators, APX/AMX flags, modes, restrictions, and relocation semantics.
- `x86_64-coverage.generated.inc` has one stable source/hash coverage row per
  complete raw XED form, linked to its normalized form ID.
- `aarch64-llvm.jsonl` contains one non-pseudo AArch64 `AArch64Inst` record
  per line.
- `manifest.json` records schema version, exact upstream provenance, selected
  XED configuration/source checksums, output checksums, counts, table sizes,
  and classification/reason totals.

## Pinned snapshot

The XED inputs are pinned to commit
`519c843c86547e2003f5a404a53358a7dcfb82f3` (`v2026.07.15`). Configuration
discovery parses every `files*.cfg` under `datafiles`, follows only
`enc-instructions` ownership entries, rejects unknown/malformed entries,
path escapes, linked files, and duplicate source ownership, then sorts paths
before hashing and importing. This includes the baseline `xed-isa.txt` and
the AMD, XOP, X87, legacy, and extension sources selected by the pinned
configuration.

The pinned import currently contains 11,013 complete XED forms, 1,995
iclasses, 5,855 iform names (3,488 forms have no IFORM), and 32,813 operands.
Coverage is `DIRECT=0`, `NORMALIZED=10,636`, `NOT64=268`, `PRIVILEGED=109`,
`RESERVED=0`, `UNSUPPORTED_TOKEN=0`, and `UNCLASSIFIED=0`.

## Regeneration

Generate LLVM's input from the matching checkout:

```sh
llvm-tblgen \
  -I llvm/lib/Target/AArch64 \
  -I llvm/include \
  llvm/lib/Target/AArch64/AArch64.td \
  -dump-json -o AArch64.json
```

Run the repository-owned importer from the repository root:

```sh
./build.sh import_assembly_metadata \
  /path/to/xed/datafiles \
  /path/to/AArch64.json \
  src/buster/lib/compiler/assembly/generated
```

The importer runs repository-owned parser, configuration, schema, unknown
token, coverage, checksum, and determinism tests before writing outputs. Run
it twice into separate directories and compare all five files when changing
the importer:

```sh
cmp first/x86_64-xed.jsonl second/x86_64-xed.jsonl
cmp first/x86_64-assembly.generated.h second/x86_64-assembly.generated.h
cmp first/x86_64-coverage.generated.inc second/x86_64-coverage.generated.inc
cmp first/aarch64-llvm.jsonl second/aarch64-llvm.jsonl
cmp first/manifest.json second/manifest.json
```

XED data is Apache-2.0. LLVM data is Apache-2.0 WITH LLVM-exception. Exact
URLs, revisions, checksums, and generated sizes are in `manifest.json`.
