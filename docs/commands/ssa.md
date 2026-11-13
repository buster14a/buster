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
  verifies the promoted IR. New phi prefixes use canonical local order
  before assigning value IDs (#1395), independent of constructor discovery
  order. `test-ssa-ir` compares typed-stream, all-memory IR and both promoted
  forms, with and without frame objects and copies, and requires identical
  serialized loop IR where first-read and local orders differ.
  `test-ssa-source [--typed] [--generate N] [--seed N] [--clang PATH] [--timeout SECONDS]
  [--steps N] [--lifetimes] [--tier1] [--keep] [preprocessor options] [file...]`
  (`ssa_source_test.c`, #134) promotes every function `-c` compiles of C files,
  and of random programs of structured and unstructured control flow
  (`ssa_source_generate.c`, each built by clang -O0 for its output), by both paths
  with `ssa_verify`'s checks on each promotion (`ssa_ir_promote_verified`), then
  runs each program with a main by the shared IR's interpreter in its three forms
  (every local in memory, and each path's SSA), which must agree and give its
  expected output. With `--lifetimes`, the IR has the blocks' lifetime markers, as
  `--tier1`'s has, and promotion makes a block's locals undefined at each start
  of their block (#141: `ssa_ir_accesses`, the [SSA guide](../../docs/ssa.md)'s
  adapter), which Tier 1's CFG consumer moves nothing for (`tier1_cfg.c`'s
  undefined phi operands).
  `--tier1` also compiles each executable source through the driver's strict
  Tier 1 path, by the default and scalar host paths, then compares both native
  programs' status and output with the interpreted forms. Compilation must
  succeed with every emitted function compiled by Tier 1; a refusal or Tier 0
  fallback fails. Native builds and executions follow the interpreter children
  sequentially, so the command keeps the same CPU grant. Given more than one
  CPU (`BUSTER_PARALLEL_CPUS`, `test_process_cpus`), the files and generated
  programs run as parts (`test_process.c`) on the command's CPUs as tokens in
  a pipe, as make's jobserver keeps them: a part holds one from its start to
  its end, and its forms run at once on as many more as it can borrow, so that
  a long program's forms spread over the CPUs the others leave at the end. The
  parts' lines come in the items' order (#2658): the c-testsuite comparison,
  one file at a time, kept 0.70 of its 4 CPUs busy in every hosted sanitizer
  row.
  `--typed` adds the production typed interpreter as a fourth form. It runs the
  retained typed stream directly; no IR is executed for that form. The source
  suite and random-program CI commands enable it, including the subprocess
  failure controls in `--self-test`. `--self-test --tier1` also checks the native
  comparison's launch, compilation, timeout and output failure boundaries.
  Failed hosted comparisons retain generated C, compiler logs and captured
  output in the shard's `ssa-source-*` artifact; executable binaries are omitted.
  [Stage 4 acceptance](../type-checking-ssa.md) maps #130's requirements to
  these commands and the existing corpus and 9700X records.
