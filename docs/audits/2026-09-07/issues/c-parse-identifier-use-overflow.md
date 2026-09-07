<!-- buster-audit-2026-09-07:c-parse-identifier-use-overflow -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated out-of-bounds write in the Release configuration.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

A typedef name at the start of a statement is bound twice: once as a declaration-specifier at `c_parse.c:13996`, and again as a plain identifier use at `c_parse.c:14058` when the declaration parse falls through. But `identifier_use_capacity` is sized `identifier_count + 1` at `c_parse.c:15420`, which allows one use per identifier token.

The write at `c_parse.c:11722` is bounded by a `BUSTER_CHECK`, which is `__builtin_unreachable()` in Release (see `release-check-macro-is-unreachable`). So Debug aborts and Release writes past the arena allocation.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_parse.c:11722 (`c_parse_bind_identifier_entity`, the write)
- src/buster/lib/compiler/frontend/c/c_parse.c:15420 (capacity sizing)
- src/buster/lib/compiler/frontend/c/c_parse.c:13996 and :14058 (the two bind sites)

## Reproduction and measured evidence

```c
typedef int B; void n(void){ B{}B{}B{}B{}B{}B{}B{}B{} }
```

54 bytes. Eight identifier tokens, each bound twice: 16 uses against a capacity of 15.

```
ide (Debug): assertion failed at c_parse.c:11720 in c_parse_bind_identifier_entity
```

In Release the check is `__builtin_unreachable()` and the write proceeds. The overrun scales linearly with input: an instrumented run of `typedef int B; void n(void){` + `B{}` x 20000 recorded `overflow_writes=19993 capacity=20007` - about 240 KB written past the end, directly into the immediately following bump allocations `identifier_use_by_token`, `token_classes` and `diagnostics` (`c_parse.c:15480-15487`). Corrupted `token_classes` bytes are read back by `c_parse_token_class` and change identifier classification.

Also reachable via `B[];` and `{}B*x;` repeated.

The input is invalid C - clang rejects it too - so this is a malformed-input path, not a valid-program path. It is nonetheless an unchecked write in the shipping configuration, and the corruption is silent.

## Required fix

In `c_parse_bind_block_statements`, skip the second bind when `declaration_type_start == index` and the specifier bind already ran. Alternatively bound-check in `c_parse_bind_identifier_entity` and drop the excess use - but the double bind is itself the defect, so removing it is preferable to tolerating it.

## Validation / definition of done

Add the 54-byte reproducer as a parser diagnostic test and assert both the diagnostic and the recorded use count. Run it in a **Release** build with the bound retained (see `release-check-macro-is-unreachable`) or under ASan - a Debug-only test passes for the wrong reason here.

Worth fuzzing the statement-start declaration/expression ambiguity more broadly; this is the shape that produced it.

## Existing work / scope

Section S3 of the 2026-09-07 audit.
