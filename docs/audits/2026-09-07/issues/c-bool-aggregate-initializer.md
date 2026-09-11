<!-- buster-audit-2026-09-07:c-bool-aggregate-initializer -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation in Release and a compiler abort in Debug, both on valid C.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`c_ir_constant_initializer_store_integer_leaf` masks the initializer value to the member's `bit_width` rather than comparing it against zero. `_Bool` has `bit_width == 1`, so any **even** nonzero constant becomes `false`. C 6.3.1.2 requires conversion to `_Bool` to yield 1 for any nonzero value.

Two failure modes from one bug. In Release the wrong value is stored silently. In Debug the differential gate `c_ir_constant_initializer_check_literal_leaf` (`c_gen.c:38014`) notices the disagreement and aborts the compiler - so the Debug build refuses to compile valid C, and the Release build miscompiles it. The gate is `#if !BUSTER_OPTIMIZE`, so the configuration that ships has no defence.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:37874 (`c_ir_constant_initializer_store_integer_leaf`)
- src/buster/lib/compiler/frontend/c/c_gen.c:38014 (`c_ir_constant_initializer_check_literal_leaf`, the Debug-only gate)

## Reproduction and measured evidence

```c
#include <stdio.h>
struct S { _Bool b; };
struct S s = { 2 };
_Bool a[3] = {2, 256, 1};
int main(void) { printf("b=%d a=%d,%d,%d\n", (int)s.b, (int)a[0], (int)a[1], (int)a[2]); return 0; }
```

```
clang 18     : b=1 a=1,1,1
ide (Release): b=0 a=0,0,1
ide (Debug)  : assertion failed at c_gen.c:38014 in c_ir_constant_initializer_check_literal_leaf
```

Also triggered by `{4}`, `{256}`, `{2147483648u}`, `{-2}`, `{'\2'}`, and by an enum constant with an even value.

## Required fix

Store `value != 0` when `child->kind == IR_TYPE_BOOLEAN`, or exclude boolean children from `c_ir_constant_initializer_leaf_class` so they take the general conversion path. Do not widen the mask - the defect is that a mask is being used where a boolean conversion is required.

## Validation / definition of done

Add initializer tests for `_Bool` members and `_Bool` arrays with even, odd, large, and negative constants, in struct, array, and nested-designator forms, static and automatic.

While here, consider whether the Debug-only gate at `c_gen.c:38014` should abort or should fall back to the slow path and emit a diagnostic; aborting the compiler on valid C is worse than the miscompile it is guarding against. See the `release-check-macro-is-unreachable` issue for the general form of this problem.

## Existing work / scope

Section S1 of the 2026-09-07 audit. Shares the differential gate with `c-unsigned-to-float-constant-fold`.
