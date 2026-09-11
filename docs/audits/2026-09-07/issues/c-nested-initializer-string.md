<!-- buster-audit-2026-09-07:c-nested-initializer-string -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated miscompilation of ordinary C, no diagnostic.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`c_ir_lower_nested_compound_literal_step` descends into aggregate members with a brace-elision loop but never checks whether the initializer for a char array is an unbraced string literal. The string is brace-elided into element 0 instead of being recognised as a whole-array string initializer, so a single (wrong) byte is stored and the rest of the array is left zero.

The task-level branch at `c_gen.c:20624` already handles the braced form correctly; the nested walker has no equivalent.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_gen.c:20867 (`c_ir_lower_nested_compound_literal_step`)

## Reproduction and measured evidence

```c
#include <stdio.h>
#include <string.h>
struct Point { int x, y; };
struct Cfg { struct Point origin; char name[16]; int flags; };
int main(void) {
    struct Cfg c = { .origin = {3,4}, .name = "widget", .flags = 7 };
    printf("name='%s' len=%zu flags=%d\n", c.name, strlen(c.name), c.flags);
    return 0;
}
```

```
clang 18 : name='widget' len=6 flags=7
ide      : name='\xef'   len=1 flags=7
```

The trigger is narrow but the shape is ordinary. It fires only when the initializer also contains a nested brace or designator, which is what routes it to the nested walker: `{ .name = "widget" }` alone is correct, and the `static` version is correct. The positional form `struct { struct In i; char s[4]; } n = { {1,2}, "cd" };` fails identically.

## Required fix

Before the aggregate-descent `while` loop, take the string branch when the member is a character array and the initializer tokens are a string literal: `c_ir_tokens_are_string_literals(...)` followed by `c_ir_emit_string_range_typed` and `c_ir_emit_store_place`, exactly as `c_gen.c:20624` does.

## Validation / definition of done

Add tests covering a char array member initialized by a string literal in: designated and positional nested initializers, automatic and static storage, arrays of structs, and the case where the string is shorter, exactly as long, and one shorter than the array (no NUL). Assert the full array contents including the zero tail, not just `strlen`.

## Existing work / scope

Section S1 of the 2026-09-07 audit.
