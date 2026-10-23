# Selective lowering qualification, 2026-10-23

This records the standalone emission planner introduced by PR #325 for #129.
The compiler sources are from `5a9e05048c7c4494491bf2eb62d9f326cf940218`;
`3335607dc130e93e1fc520bf0b8d83cf44065f63` adds its CI benchmark invocation.
Neither revision supplies the complete semantic source producer or backend
integration. These results do not measure actual bodies lowered or `main.c`
compile time saved.

The required [9700X CI run 39935948376](https://github.com/buster14a/buster/actions/runs/39935948376)
passed with Clang 23.1.1. It tested merge
`35690a73d0262e81bef8ea768d94fe6c0de9239d`, combining PR head `3335607` with
main `635434f2a4d153c6de1dd2f1149055c4507c6f46`. The runner hooks held the
shared host lock for the job.

- `test-lowering --clang`: 3,990,695 checks, zero failures, 14 manually
  modeled C fixtures, all 56 required Clang object-symbol comparisons.
- Parser corpus regression: 24 repository translation units, 17,343,750
  tokens, all accepted by Clang, zero mismatches or scalar/vector differences.
- No-recursion check: 32 translation units, 16,605 functions, 90,780 calls,
  zero disallowed cycles; the three pre-existing allowed cycles remain.
- All six lowering benchmark shapes passed their independent result checks.

[The raw CI benchmark output](ci-benchmark.log) retains the single process's
21-sample medians for vector selection, scalar selection and preparation plus
vector selection. The workflow pins this smoke run to its first allowed CPU.
These are unconditioned single-operation observations. They establish neither
sustained vector timing nor repeatability across processes. The vector path
was faster on the small-live-set, chain, cycle and large cases and slower on
fanout and duplicate-heavy cases; no general vector-speedup claim follows.
Preparation is included in its own measurement, but there is no
preparation-plus-scalar comparison.

The separate target build has SHA-256
`4c634879d239980d271ddb60cd64f3f44c18c685457ef0756b759e7c2b77d5d2`.
Every file in [the source manifest](source.sha256) matched the target tree.
Both that normal build and a focused AddressSanitizer/UndefinedBehaviorSanitizer
build completed the same 3,990,695 checks and all 56 Clang comparisons with
zero failures. Build and test commands acquired the shared lock separately.
The sanitizer compile used Clang 23.1.1 with `-O1 -g -march=native -fwrapv
-funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function
-fsanitize=address,undefined -fno-omit-frame-pointer -pthread`. Its entry point
included `lane.c`, `c_lowering.c`, `c_lowering_test.c` and
`c_lowering_benchmark.c` and called `run_c_lowering_tests(argc - 1, argv + 1)`.

The exact [focused entry point](sanitizer-main.c) is retained here. On the
approved host, from `/home/david/buster-fast-bench/codex-lowering-129`, the
frozen checkout was `tree/` and this entry point was `lowering-main.c`.
The sanitizer commands were:

```sh
EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh \
  clang -O1 -g -march=native -Itree/src -fwrapv -funsigned-char \
  -fno-strict-aliasing -Wall -Werror -Wno-unused-function \
  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  lowering-main.c -o lowering-sanitize
EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh ./lowering-sanitize --clang
```

The requested-oracle failure control ran the normal binary with
`test-lowering --clang /bin/false` under its own shared lock. It exited 1,
reporting 57 expected failures and zero Clang comparisons. The shell accepted
only that exact exit status. [The diagnostic output](clang-negative.log)
records the failed comparisons; no successful external check is inferred from
this deliberately failing run.

The semantic handoff and outstanding source/corpus acceptance are described
in [the lowering guide](../../c-lowering.md).
