<!-- buster-audit-2026-09-07:x64-atomic-16-byte-aggregate -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P2 - valid C rejected with a hard error; no silent wrong answer.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

The CMPXCHG16B path requires `value_type->kind == IR_TYPE_INTEGER`. A 16-byte `_Atomic` **struct** therefore falls through to the size gate at `codegen.c:12769` and is rejected, even though the operation only needs the address and two eightbyte pairs, and the identically laid out `_Atomic unsigned __int128` compiles and runs correctly.

**Source locations**

- src/buster/lib/compiler/codegen/codegen.c:12769 (`codegen_generate_canonical_module_attempt`, IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)

## Reproduction and measured evidence

```c
typedef struct { long long a, b; } P16;
_Atomic P16 ap;
/* ... */
atomic_compare_exchange_strong(&ap, &e, w);
```

```
ide: cc: error: C code generation failed with error 2
```

The `__int128` spelling of the same 16-byte operation works.

## Required fix

Widen the 16-byte branch's type test to accept any 16-byte scalar-or-aggregate `_Atomic` object, rather than testing for `IR_TYPE_INTEGER`.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add tests for `_Atomic` structs of 16 bytes with differing member shapes (two `long long`, four `int`, pointer plus counter), covering compare-exchange strong and weak, load, store and exchange. Check the alignment requirement is enforced.

## Existing work / scope

Section S2 of the 2026-09-07 audit.
