<!-- buster-audit-2026-09-07:codeview-gproc32-pend-offsets -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated invalid scope pointers in emitted PDBs.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`codeview_build_legacy` patches `S_GPROC32`'s `pEnd` with `end_record`, an offset into the whole `.debug$S` blob. But `pdb_split_codeview` (`pdb.c:252-256`) writes only the concatenated `DEBUG_S_SYMBOLS` **payloads** into the PDB module stream, stripping the signature and the subsection headers.

Every emitted `pEnd` is therefore too large by exactly the stripped bytes, and a DIA or MSVC reader following it walks past the symbol region into the C13 line-table region.

**Source locations**

- src/buster/lib/compiler/codeview/codeview.c:734 (`codeview_build_legacy`, the pEnd patch)
- src/buster/lib/compiler/pdb/pdb.c:252-256 (`pdb_split_codeview`)

## Reproduction and measured evidence

```sh
ide cc -g --target=x86_64-pc-windows-msvc t1.c -o t1.exe
llvm-pdbutil dump --symbols t1.pdb
```

`S_GPROC32 'add'` reports `end = 228` while its `S_END` is at 204; `main` reports `end = 572` while the symbol region ends at 360.

At 40000 functions the divergence is large: `main` at offset 3680056 claims `end = 7200124` in a 3680104-byte symbol region.

## Required fix

Track the running module-symbol offset - subsection payload bytes only - and patch `pEnd` and `pParent` with that, or rewrite the scope pointers in `pdb_build` after the split. The second option keeps the knowledge of the stripped bytes in the one function that does the stripping.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then validate emitted PDBs with `llvm-pdbutil dump --symbols` and assert every `pEnd` resolves inside the symbol region and points at the matching `S_END`. Cover nested lexical scopes and inlined frames, not just top-level functions.

## Existing work / scope

Section S4 of the 2026-09-07 audit. Windows targets only. Independent of `codeview-fieldlist-length-overflow` despite being in the same file.
