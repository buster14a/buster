<!-- buster-audit-2026-09-07:c-unsigned-to-float-constant-fold -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation of valid C.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

The INTEGER_TO_FLOAT arm of `c_ir_constant_initializer_fold_integer_leaf` converts through `c_ir_integer_signed_value`, an `s64`, regardless of the literal's signedness. Any unsigned literal at or above 2^63 is reinterpreted as negative before the conversion to floating point.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:37922 (`c_ir_constant_initializer_fold_integer_leaf`)

## Reproduction and measured evidence

```c
#include <stdio.h>
struct F { double d; };
struct F f = { 18446744073709551615ULL };
int main(void) { printf("d=%.17g\n", f.d); return 0; }
```

```
clang 18     : d=1.8446744073709552e+19
ide (Release): d=-1
ide (Debug)  : assertion failed at c_gen.c:38014
```

`{ 9223372036854775808ULL }` yields `-9223372036854775808`. Same for `float` members.

## Required fix

Route this arm through `c_ir_constant_integer_to_float`, which already handles unsigned values and rounds at the destination precision, rather than casting through a host `s64`.

## Validation / definition of done

Add tests for unsigned literals at 2^63, 2^63+1, and `UINT64_MAX` initializing `float`, `double` and `long double` members, in struct and array forms. Verify the rounding matches the runtime conversion path.

Run the C frontend module suite and `test_all`.

## Existing work / scope

Section S1 of the 2026-09-07 audit. Same Debug gate as `c-bool-aggregate-initializer`; they are separate root causes and should be fixed separately.
