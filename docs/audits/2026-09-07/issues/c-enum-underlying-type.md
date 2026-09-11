<!-- buster-audit-2026-09-07:c-enum-underlying-type -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation of valid C, no diagnostic.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`c_lower_to_ir` maps an enumeration without a C23 fixed underlying type unconditionally to `s32_type` instead of selecting the type from the enumerator range. `c_ir_emit_integer_value` (`c_gen.c:24192`) hardcodes the same choice on the value path, and `c_ir_constant_identifier` on the constant path.

C requires the implementation to choose an underlying type capable of representing every enumerator. Any enumerator outside `int` range is therefore truncated or sign-extended, and `sizeof` reports the wrong size.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:42413 (`c_lower_to_ir`)
- src/buster/lib/compiler/frontend/c/c_gen.c:24192 (`c_ir_emit_integer_value`)

## Reproduction and measured evidence

```c
#include <stdio.h>
enum Flags { F_NONE = 0, F_HIGH = 0x80000000 };
enum B { BB = 4294967296LL };
int main(void) {
    printf("high_gt0=%d bb=%lld sz=%zu\n", F_HIGH > 0, (long long)BB, sizeof(enum B));
    return 0;
}
```

```
clang 18 : high_gt0=1 bb=4294967296 sz=8
ide      : high_gt0=0 bb=0          sz=4
```

`BB` collapses to zero. `F_HIGH` compares as negative, so any enum used as a bit-flag set with bit 31 set is silently wrong at every comparison and every widening use. Both Debug and Release agree, on the default target and allocator.

## Required fix

Derive the underlying type from the enumerator range: `unsigned int` when no enumerator is negative and the maximum exceeds `INT_MAX`, `long`/`long long` when a value exceeds 32 bits, `unsigned long long` when an unsigned value exceeds `LLONG_MAX`. The rule is already written down for enum bit-fields at `c_gen.c:42773` - reuse it rather than inventing a second one. Fix all three sites (`c_lower_to_ir`, `c_ir_emit_integer_value`, `c_ir_constant_identifier`) together; fixing one leaves the constant and value paths disagreeing.

## Validation / definition of done

Add a frontend test covering: an enumerator at `0x80000000`, one at `2^32`, one negative alongside a large positive, and `sizeof` for each. Assert against the values in the reproduction above, not against self-consistency.

Run the C frontend module suite and `test_all`. Note that Release self-host cannot catch this class of defect: it requires stage 1 and stage 2 to be byte-identical, and a compiler that miscompiles enums identically in both generations still reaches a fixed point. A differential run against clang or gcc is the gate that catches it.

## Existing work / scope

One of seven silent-miscompilation findings in the 2026-09-07 audit; see `AUDIT_REPORT.md` section S1. Independent of the eBPF/Wasm/runtime work in the 2026-09-06 bundle.
