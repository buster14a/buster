<!-- buster-audit-2026-09-07:driver-default-object-path -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated divergence from gcc and clang; breaks out-of-tree and read-only builds.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`compiler_driver_default_object_path` builds the default object path by replacing the extension of the **full input path**, instead of taking the input's basename and resolving it against the working directory. The same logic is duplicated at `driver.c:3765`.

gcc and clang both write `./<basename>.o`.

**Source locations**

- src/buster/lib/compiler/driver/driver.c:3401 (`compiler_driver_default_object_path`)
- src/buster/lib/compiler/driver/driver.c:3765 (duplicate of the same logic)

## Reproduction and measured evidence

```sh
mkdir -p srcdir outdir && echo 'int f(void){return 1;}' > srcdir/a1.c
cd outdir && ide cc -c ../srcdir/a1.c
```

```
srcdir/a1.o exists? YES
outdir/a1.o exists? no
```

gcc and clang write `outdir/a1.o`.

Consequences: out-of-tree builds deposit objects into the source tree; two build configurations of the same tree overwrite each other's objects; compiling from a read-only checkout fails outright.

## Required fix

Strip the directory prefix as well as the extension, so the result is `<basename>.o` relative to the working directory. Deduplicate the two sites while fixing - the duplication is why one could be fixed and the other missed.

## Validation / definition of done

Add driver tests for `-c` with a relative input path from a different working directory, an absolute input path, an input in the working directory, and multiple inputs with the same basename in different directories (which should collide, exactly as gcc does). Assert the output location, not just compilation success.

## Existing work / scope

Section S4 of the 2026-09-07 audit. Behavioural divergence rather than a code-generation defect, but it silently writes to a directory the user did not name.
