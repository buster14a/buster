<!-- buster-audit-2026-09-07:c-parse-initializer-infinite-loop -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated non-termination on malformed input, in Debug and Release.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

In `c_parse_infer_initializer_array_count_core`, the brace-elision branch advances the parent frame to `frame->cursor = value_end`, then pushes a `borrowed` child frame at `.cursor = designator.value_start`. When that child is immediately popped as exhausted, the pop at `c_parse.c:4884-4890` copies the child's **unadvanced** cursor back into the parent (`frames[frame_count - 1].cursor = cursor`), undoing the advance.

If the child is always exhausted on its first iteration, the parent re-processes the same value forever. That happens whenever `c_parse_initializer_type_slots` returns 0 - for example when the element type is an incomplete struct.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_parse.c:4967 (`c_parse_infer_initializer_array_count_core`, the borrowed-frame push)
- src/buster/lib/compiler/frontend/c/c_parse.c:4884-4890 (the pop that copies the cursor back)

## Reproduction and measured evidence

```c
struct i e[]={>};
```

Eighteen bytes. Both `build/Debug/ide cc` and `build/Release/ide cc` spin indefinitely; killed at 20 s and at 120 s respectively. Instrumented, the loop oscillates between frame counts 1 and 2 with the cursor pinned at 7, reaching 2,000,000 iterations with no progress.

Same behaviour for `struct s e[]={()};`, `struct i e[]={<};`, and `{[]};static const struct i e[]={>};`.

A build that touches an unlucky generated or pasted file never terminates and produces no diagnostic.

## Required fix

On popping a `borrowed` frame, propagate its cursor to the parent only when it advanced: `if (cursor > frames[frame_count - 1].cursor)`. Alternatively, refuse to push a borrowed frame whose type has zero slots. Both are correct; the first is the narrower change.

Independently, this loop should have a progress assertion - a cursor that has not advanced across a full iteration is a bug in every case, and a compiler should say so rather than hang.

## Validation / definition of done

Add all four reproducers above as parser diagnostic tests, asserting a diagnostic and termination. Add a progress guard to the frame loop so a future regression fails loudly instead of hanging.

Consider a bounded-time harness in CI for the malformed-input fixtures; a hang is the one failure mode a test suite cannot report on its own.

## Existing work / scope

Section S3 of the 2026-09-07 audit. Malformed input, not valid C - but non-termination with no diagnostic is worse than a rejection.
