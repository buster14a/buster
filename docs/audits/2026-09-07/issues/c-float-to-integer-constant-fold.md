<!-- buster-audit-2026-09-07:c-float-to-integer-constant-fold -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation of valid C, no diagnostic.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`c_ir_constant_cast` converts a floating constant to its integer representation with a bare `(u64)source.floating`. That is undefined behaviour in C whenever the value is negative or at least 2^64. Clang lowers the expression to `vcvttsd2usi`, whose out-of-range result is `0xFFFFFFFFFFFFFFFF`, so every negative floating constant converted to an integer type folds to -1.

The same line also drops the high limb for 128-bit targets: `integer_high` is never populated.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:39429 (`c_ir_constant_cast`)

## Reproduction and measured evidence

```c
#include <stdio.h>
static const long long L = (long long)(-2147483648.0);
static const int T[3] = {(int)-1.5, (int)-2.5, (int)2.5};
int main(void) { printf("L=%lld T=%d,%d,%d\n", L, T[0], T[1], T[2]); return 0; }
```

```
clang 18 : L=-2147483648 T=-1,-2,2
ide      : L=-1          T=-1,-1,2
```

Note `T[0]` is coincidentally right - truncation of -1.5 really is -1 - which is exactly the kind of accident that hides this in a spot check. `T[1]` is wrong.

128-bit form: `static __int128 F = (__int128)1.5e30;` yields `0x00000000000000000FFFFFFFFFFFFFFFF` instead of the correct value. Confirmed under gdb: `converted.integer == UINT64_MAX` from a `C_IR_CONSTANT_FLOAT` of -10.

## Required fix

Convert through `s64` when the value is negative (`source.floating < 0 ? (u64)(s64)source.floating : (u64)source.floating`), range-check the result against the destination type before storing, and populate `integer_high` when the target is 128-bit. Out-of-range conversions are UB in the source program too, but the compiler must not itself execute UB while folding - pick a defined result and be consistent with the runtime conversion path.

## Validation / definition of done

Add constant-folding tests for negative and positive floats into every integer width, signed and unsigned, plus the 128-bit destination, plus values beyond the destination range. Cross-check each against the runtime conversion path in the same compiler - the two must agree.

Run the C frontend module suite and `test_all`. Self-host cannot catch this (see the note in `c-enum-underlying-type`).

## Existing work / scope

Section S1 of the 2026-09-07 audit. Runtime float-to-int conversion was probed separately and is correct; this is the constant path only.
