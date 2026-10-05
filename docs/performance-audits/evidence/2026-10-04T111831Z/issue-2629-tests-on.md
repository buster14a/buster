## Observed

At `94976eccad9c31776cf264a33dcda960dd8ed51a`, the local default configure (`./build.sh generate` without `--no-include-tests`) keeps `BUSTER_INCLUDE_TESTS=ON`, and `src/buster/lib/compiler/ir/ir.h` defines

```c
#define BUSTER_IR_TRANSFORM_CHECKS (!BUSTER_OPTIMIZE || BUSTER_INCLUDE_TESTS || BUSTER_SANITIZE || BUSTER_VERIFY_IR_TRANSFORMS)
```

so the default local **Release** `ide` runs the checked preparation path in `ir_prepare_canonical_module` (`ir_fast.c`): the promotion-output validation (`ir_validate_promotion_output`, with the per-function pre-promotion attribution scans) and the FAST-output `ir_validate_canonical_module`, which the tests-off production compiler skips on certified input.

Measured on the stage-1 self-compile (`cc -Isrc -I<generated> -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm`, same source, same host, `perf stat -r 5`, spread 0.00%):

| Compiler | instructions | sampled extra passes |
|---|---:|---|
| default `generate` (tests on), Release `build/Release/ide` | **22,699,115,608** | `ir_validate_promotion_output` 2.35%, a second `ir_validate_canonical_module` 2.15%, `ir_validate_module_ownership` 1.46% |
| `generate --no-include-tests`, Release | **21,512,707,724** | one module validation (`preparation_fast_input_validations=1`) |

That is **+5.2% instructions (+1.19 G)** for the locally profilable binary the guide describes.

## Expected

`docs/agents/benchmarking.md` says "The local Release tree is profilable as built" and its A/B recipe builds both compilers with a plain `./build.sh generate` + `build --config Release -t ide`; AGENTS.md says to "measure using the trusted Clang-built compiler". Neither states that the trusted performance binary must be configured with `--no-include-tests`, although the recent audits that did state their configuration (for example `2026-10-03T204621Z`) built "with tests … off", and the `2026-10-03T100722Z` baseline records only "Non-CI configure: -g and frame pointers on". A reader following the guide profiles ~5% of validation work that production never runs, and absolute figures from differently configured trees are not comparable.

## Affected symbols

`BUSTER_IR_TRANSFORM_CHECKS` (`ir/ir.h`), `ir_prepare_canonical_module`, `ir_validate_promotion_output` (`ir/ir_fast.c`); `build.c` `--include-tests` default; `docs/agents/benchmarking.md` ("local Release tree is profilable as built", "Benchmarking a compiler change (A/B)" step 1).

## Validation already run

The two instruction counts above, plus `perf record` shares of the extra passes in the tests-on binary; recorded in `docs/performance-audits/2026-10-04T111831Z.md` (PR #2627, measurements file). No source changed.

## Uncertainty

Whether the baseline audit `2026-10-03T100722Z` (22.29 G instructions on the 9700X) was a tests-on or tests-off build is not stated in it; if tests-on, about 1.1 G of its total is this validation work and its phase table's `ir` share includes it.

## Completion criteria

Either of:

1. Documentation: the benchmarking guide's "profilable as built" and A/B sections state `generate --no-include-tests` as part of the trusted performance binary recipe, and `tools/uarch_lab.py` records `BUSTER_INCLUDE_TESTS` (from the build's CMake cache) in `summary.json`/`report.md` beside the binary hash; or
2. Build policy: decouple `BUSTER_IR_TRANSFORM_CHECKS` from `BUSTER_INCLUDE_TESTS` in optimized builds (keep it on for `!BUSTER_OPTIMIZE`, sanitizers and `BUSTER_VERIFY_IR_TRANSFORMS`), after confirming the registered tests that rely on the checked path set `BUSTER_VERIFY_IR_TRANSFORMS` or run in Debug.

Both options need the self-host fixed point unchanged; option 2 additionally needs `test_all` in Release and the sanitized matrix.
