# Assembly metadata

These files are deterministic, reduced encoding metadata used by Buster's
standalone assembler. Normal builds and tests use the checked-in files; they do
not download, build, or execute XED or LLVM.

- `x86_64-xed.jsonl` contains one JSON object per encodable XED form.
- `aarch64-llvm.jsonl` contains one JSON object per non-pseudo AArch64
  `AArch64Inst` record.
- `manifest.json` records the schema, exact upstream releases and commits,
  licenses, input/output checksums, and record counts.

## Regeneration

Immediately before regenerating, confirm the newest stable (non-RC,
non-nightly) XED and LLVM releases and update the pinned provenance in
`build.c` if either has advanced. Generate LLVM's input from the matching
checkout:

```sh
llvm-tblgen \
  -I llvm/lib/Target/AArch64 \
  -I llvm/include \
  llvm/lib/Target/AArch64/AArch64.td \
  -dump-json -o AArch64.json
```

Then run the repository-owned importer from the repository root:

```sh
./build.sh import_assembly_metadata \
  /path/to/xed/datafiles \
  /path/to/AArch64.json
```

An optional third argument selects another output directory. The importer runs
its synthetic parser/schema test first, rejects malformed LLVM JSON, walks XED
data without following directory symlinks or Windows reparse points, sorts all
inputs, and rewrites all three outputs. Run the same command twice and compare
the outputs when changing the importer.

The imported XED data is Apache-2.0. LLVM data is Apache-2.0 WITH
LLVM-exception. Exact upstream URLs and revisions are in `manifest.json`.
