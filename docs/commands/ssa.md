# ssa

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/ssa.c` promotes eligible locals over a completed CFG:
  shared dominance/frontier analysis and segmented SIMD renaming, with a nonrecursive
  predecessor-reading scalar twin and redundant-phi SCC cleanup. `test-ssa`
  checks both against an all-in-memory path interpreter and the SSA verifier;
  `benchmark-ssa [--only scalar|vector] [--after-scalar MS]` measures synthetic
  CFGs, including analysis and allocation. `docs/ssa.md` records the view's
  contract. `ssa_ir.c` adapts shared IR slots, rebuilds every operand form and
  verifies the promoted IR; `test-ssa-ir` compares typed-stream, all-memory IR
  and both promoted forms, with and without frame objects and copies.
  `test-ssa-source [--generate N] [--seed N] [--clang PATH] [--timeout SECONDS]
  [--steps N] [--keep] [preprocessor options] [file...]` (`ssa_source_test.c`,
  #134) promotes every function `-c` compiles of C files, and of random
  programs of structured and unstructured control flow (`ssa_source_generate.c`,
  each built by clang -O0 for its output), by both paths with `ssa_verify`'s
  checks on each promotion (`ssa_ir_promote_verified`), then runs each program
  with a main by the shared IR's interpreter in its three forms (every local in
  memory, and each path's SSA), which must agree and give its expected output.
  Tier 1's code joins that comparison once Tier 1 compiles general CFGs (#138).
