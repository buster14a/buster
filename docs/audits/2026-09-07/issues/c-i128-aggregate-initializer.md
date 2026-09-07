<!-- buster-audit-2026-09-07:c-i128-aggregate-initializer -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated silent data loss in emitted .data.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`c_ir_constant_initializer_bytes_legacy_core` writes each leaf through `c_ir_constant_store_bits(..., bits, sign_extend)`, which takes a single `u64`. A 128-bit member is therefore written as its low 64 bits sign-extended to 16 bytes, and `converted.integer_high` is discarded.

The scalar form is correct because it takes the `IR_GLOBAL_INITIALIZER_BYTES` path instead; only array and struct members go through this writer.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:35253 (`c_ir_constant_initializer_bytes_legacy_core`)
- src/buster/lib/compiler/frontend/c/c_gen.c:35522 (same writer, second site)

## Reproduction and measured evidence

```c
__int128 g4[2] = { ((__int128)1 << 100), (__int128)0xffffffffffffffffULL };
int main(void) { return 0; }
```

`objdump -s -j .data`:

```
clang 18 : 00000000 00000000 00000000 10000000     <- 2^100
           ffffffff ffffffff 00000000 00000000     <- 2^64-1
ide      : 00000000 00000000 00000000 00000000     <- 0
           ffffffff ffffffff ffffffff ffffffff     <- -1
```

Both elements are wrong in different ways: the first loses its value entirely, the second is sign-extended into a different number. `__int128 g1 = ((__int128)1 << 100);` as a scalar is correct.

## Required fix

Give the aggregate leaf writer a 128-bit path that stores both `converted.integer` and `converted.integer_high` when `type->layout.size == 16`, rather than funnelling through the `u64` helper.

## Validation / definition of done

Add tests for `__int128` and `unsigned __int128` members in arrays and structs, with values above 2^64, at exactly 2^64-1, negative, and `INT128_MIN`. Verify emitted `.data` bytes, not just runtime reads - a runtime read that goes through the same wrong path can agree with itself.

## Existing work / scope

Section S1 of the 2026-09-07 audit.
