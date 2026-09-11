<!-- buster-audit-2026-09-07:x86-att-s-suffix-alias -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - demonstrated emission of a different instruction than the one written.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

`assembly_x86_metadata_suffix_alias` maps `suffix == 's'` to a base mnemonic plus 32-bit width. This is intended for the x87 forms that use `s`/`t` as precision suffixes, but it is applied to every mnemonic ending in `s`.

When the exact `bts` form is rejected - which happens for a memory operand of unspecified width - the fallback strips the `s`, resolves `bt`, and emits BT's `/4` opcode extension instead of BTS's `/5`. The memory bit is read and never set.

**Source locations**

- src/buster/lib/compiler/assembly/assembly.c:11154-11158 (`assembly_x86_metadata_suffix_alias`)

## Reproduction and measured evidence

```asm
bts $3, (%r23)
```

```
gas / clang : d5 90 ba 2f 03    btsl $0x3,(%r23)
ide         : d5 90 ba 27 03    btl  $0x3,(%r23)     <- reads the bit, never sets it
```

Also reproduced for `bts $3, (%r16)`, `bts $3, 16(%r23)`, `bts $3, (%rax,%r23,4)` and `bts $65, (%r23)`.

The same alias makes the assembler accept things that are not instructions, silently assembling them as 32-bit operations: `adds $3,(%rax)` -> `addl`, `cmps $3,(%rax)` -> `cmpl`, `movs $3,(%rax)` -> `movl`. gas rejects all three.

## Required fix

Restrict the `s` and `t` suffix widths to the x87 opcodes that actually use them - gate on `suffixed_info.suffix_width` or the x87 alias table - instead of applying them to every mnemonic. Separately, the exact-form path should not silently fall back to a *different mnemonic*; a rejected exact form should produce a diagnostic, not a resolution of a shorter name.

## Validation / definition of done

First confirm the reproduction above; this finding comes from a subsystem pass and was not re-run for this bundle.

Then add negative tests asserting that `adds`, `cmps`, `movs` with immediate operands are rejected, and positive tests that `bts`/`btr`/`btc` with unsized memory operands either encode correctly or produce a diagnostic. The second half of the fix (no silent mnemonic fallback) is the one that prevents the next instance of this class.

## Existing work / scope

Section S2 of the 2026-09-07 audit.
