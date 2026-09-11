<!-- buster-audit-2026-09-07:dwarf-ranges-double-count-base -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated failure of llvm-dwarfdump --verify on linked output; debuggers cannot locate any function.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`dwarf_model_emit_ranges` emits range-list entries with `.address = true` relocations - absolute addresses - while `dwarf_build_model` also emits the CU's `DW_AT_low_pc` as an absolute relocated address.

Per DWARF-4 section 2.17.3, the CU's `DW_AT_low_pc` is the base to which every `DW_AT_ranges` entry is added. Emitting both as absolute makes consumers add the text base twice.

`.debug_loc` lists at `dwarf.c:1143-1156` have the identical defect.

**Source locations**

- src/buster/lib/compiler/dwarf/dwarf.c:1060 (`dwarf_model_emit_ranges`)
- src/buster/lib/compiler/dwarf/dwarf.c:1801-1806 (`dwarf_build_model`, the CU low_pc)
- src/buster/lib/compiler/dwarf/dwarf.c:1143-1156 (`.debug_loc`, same defect)

## Reproduction and measured evidence

```sh
ide cc -g -c dw.c -o dw.o && ld -o dw_gnu -e main dw.o
llvm-dwarfdump --verify dw_gnu
```

```
error: DIE address ranges are not contained in its parent's ranges:
  DW_TAG_compile_unit   DW_AT_low_pc (0x0000000000401000)
  DW_TAG_subprogram "main"   DW_AT_ranges ([0x0000000000802030, 0x0000000000802078))
Errors detected.
```

`main`'s code is at 0x401030; its DIE claims 0x802030 - the text base added twice. Every subprogram, lexical block and inline site is unreachable by address in gdb and lldb.

An unlinked `.o` verifies clean, which is exactly why this survived: the defect only appears once a nonzero text base exists. It reproduces with buster's own linker as well, as soon as there are two or more translation units.

## Required fix

Emit the CU's `DW_AT_low_pc` as a literal 0 with no address relocation - the convention LLVM uses when children carry `DW_AT_ranges` - or keep the range-list entries CU-relative rather than absolute. Apply the same change to `.debug_loc`.

## Validation / definition of done

Add `llvm-dwarfdump --verify` (or the equivalent) over **linked** output to the debug-info tests, not just over objects. Cover a single TU and a multi-TU link. A gdb smoke test that breaks on a function by name and checks the resolved address would catch the user-visible half.

## Existing work / scope

Section S4 of the 2026-09-07 audit. `-g` output is affected on every ELF target.
