<!-- buster-audit-2026-09-07:c-import-once-path-unchecked -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below were re-derived for that commit and each cited site still reads as described, but the reproduction itself was not re-run there.

**Priority:** P1 - unchecked write with a demonstrated overrun.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

```c
once_paths[once_path_count++] = include_path;
```

No bound. The sibling `#pragma once` writer at `c_source.c:5872` does check (`!found && *context.once_path_count < context.once_path_capacity`); this one does not.

The array is sized `source.length + 1` at `c_source.c:7516` - from the **root** file only. A small root file that imports a header containing many distinct `#import` directives therefore overruns it, writing `String8{pointer, length}` pairs past the end of the allocation.

**Source locations**

- src/buster/lib/compiler/frontend/c/c_source.c:7902 (`c_preprocess`, the unchecked write)
- src/buster/lib/compiler/frontend/c/c_source.c:7516 (capacity sizing from the root file)
- src/buster/lib/compiler/frontend/c/c_source.c:5872 (the `#pragma once` writer, which checks)

## Reproduction and measured evidence

`root.c` containing exactly `#import "a.h"` (12 bytes, so capacity 13), where `a.h` holds 20000 distinct `#import "gN.h"` lines.

An instrumented build recorded 19988 writes past the end - about 320 KB - overwriting whatever the same arena allocated next: guard-table arrays, `CPpStampTable` entries, macro records, per-include lexed token arrays. No diagnostic, no crash.

`ide cc root.c` exercises the identical path.

## Required fix

Guard the write exactly as `c_source.c:5872` does, and size the array from the include closure rather than the root file's length. The capacity expression is the deeper defect: root length has no relationship to the number of distinct imported paths.

## Validation / definition of done

Add a preprocessing test with a small root and a header carrying many distinct `#import` directives, asserting correct once-semantics and no overrun. Run under ASan, and in a Release build if the bound is made unconditional.

While here, check whether any other capacity in `c_preprocess` is sized from the root file's length rather than from the closure it actually bounds.

## Existing work / scope

Section S3 of the 2026-09-07 audit. `#import` is a non-standard extension, but it is a live path in this preprocessor.
