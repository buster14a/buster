<!-- buster-audit-2026-09-07:x64-i128-signed-divide -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation of valid C, no diagnostic.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

In the x86-64 128-bit divide lowering, R9 holds the divisor's sign mask and R8 the dividend's. The `sign_xor` row (`XOR R9, R8`, emitted at `codegen.c:15137`) overwrites R9 with the combined result sign **before** the divisor-magnitude rows at `codegen.c:15143-15146` read it:

```
XOR RCX, R9
XOR RSI, R9
SUB RCX, R9
SBB RSI, R9
```

So the divisor is negated iff the two operand signs *differ*, rather than iff the divisor is negative. With a negative dividend and a positive divisor, the divisor is turned into its two's complement (2^128 - d); the restoring division loop then terminates immediately, yielding quotient 0 and remainder = dividend.

The AArch64 sibling `codegen_canonical_a64_i128_divide` (`codegen.c:8104`) is correct - it keeps the divisor's own sign in x16 and the combined sign in x17.

**Source locations**

- src/buster/lib/compiler/codegen/codegen.c:15092 (`codegen_generate_canonical_module_attempt`, signed_division block at :15070-15149)

## Reproduction and measured evidence

```c
#include <stdio.h>
int main(void) {
    volatile long long a = -100, b = 7;
    __int128 x = a, y = b;
    printf("q=%lld r=%lld\n", (long long)(x / y), (long long)(x % y));
    return 0;
}
```

```
clang 18 : q=-14 r=-2
ide      : q=0   r=-100
```

`(-100)/(-7)` returns 0 as well. All four allocators are affected - the machine selectors leave i128 divide to this canonical lowering. Disassembly of the emitted object shows `xor %r8,%r9` at 0x7e feeding `xor %r9,%rcx` at 0x91, which is the bug directly.

Unsigned 128-bit divide is correct, and so is the constant-folded static-initializer path, so only the emitted signed sequence is at fault.

## Required fix

Emit `sign_xor`/`save_sign` **after** `divisor_high_sbb`. This is a pure reordering: R9 then still carries the divisor's own sign mask through the divisor negation, and the combined sign is computed once the magnitude rows no longer need R9.

## Validation / definition of done

`tests/basic_c_int128.c:66-70` currently misses this because its only assertion is `q*37 + r == n`, which `q=0, r=n` satisfies trivially. Strengthen it to compare against exact expected quotients and remainders, and cover all four sign combinations plus `INT128_MIN / -1`.

Run the codegen module suite, `test_all`, and Release self-host across all four allocators.

## Existing work / scope

Section S1 of the 2026-09-07 audit. The AArch64 path is correct and can serve as the reference for the fix.
