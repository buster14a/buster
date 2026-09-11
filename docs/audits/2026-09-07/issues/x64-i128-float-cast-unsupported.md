<!-- buster-audit-2026-09-07:x64-i128-float-cast-unsupported -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P2 - valid C rejected with a hard error; no silent wrong answer.
**Evidence:** found by a subsystem pass and traced in source; the reproduction below was run there and not independently re-executed for this bundle. Confirm it before starting the fix.

## What is wrong

The i128 arm of `IR_OPCODE_CAST` returns `CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION` whenever the other side of the cast is not `IR_TYPE_INTEGER`. AArch64 has `codegen_canonical_a64_i128_to_float` (`codegen.c:8202`) and `codegen_canonical_a64_float_to_i128` (`codegen.c:8381`); x86-64 has no counterpart, and the machine selector has none either.

**Source locations**

- src/buster/lib/compiler/codegen/codegen.c:11608 (`codegen_generate_canonical_module_attempt`, IR_OPCODE_CAST i128 path)

## Reproduction and measured evidence

```c
__int128 a;
double d = (double)a;
```

```
ide: cc: error: C code generation failed with error 2 ... opcode 27
```

Valid C is rejected outright on the default target. The reverse direction `(__int128)someDouble` fails the same way.

## Required fix

Add x86-64 counterparts to the two AArch64 helpers and dispatch them from the same `source_integer128` / `target_integer128` branch. The AArch64 implementations are the reference for the rounding and sign handling.

## Validation / definition of done

First confirm the reproduction; this comes from a subsystem pass. Then add tests covering both directions for `float`, `double` and `long double`, signed and unsigned 128-bit, including values above 2^64, negative values, and rounding at the representable boundary. Cross-check against the AArch64 path, which is expected to agree.

## Existing work / scope

Section S2 of the 2026-09-07 audit. A hard error rather than a miscompile, hence P2 despite being a functional gap.
