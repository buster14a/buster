# Preflight amendment (before any grid observations)

The first runner's patch anchor `LlvmBcTypeRecord* record = context->types + index` occurs in both interning and serialization. The exact-base overlay guard refuses it. Scope the replacement to `llvm_bc_add_type_record`; do not weaken the blob/anchor check.

Current `driver.c:1664–1670` explicitly rejects `-fverify-codegen` with `-emit-llvm`. Remove that native-only flag from the predeclared LLVM commands. The canonical boundary is not weakened: `compiler_driver_llvm_bitcode_options` enables validation and `llvm_bitcode_emit_with_options` calls `ir_prepare_canonical_module` before type planning. All other grid dimensions/flags/count predictions are unchanged. The initial attempt has not executed any generated-grid compiler command at this amendment.

Retain the pristine repository LLVM source before applying the disposable overlay for the representative source probe; add a source consisting of the real `ir.h` include plus a constant-return function so unsupported body operations cannot obscure header-graph reach. Neither changes the predeclared grid.

Add an independent Python standard-library evidence checker that reconstructs the old sweep from logged canonical graph edges and compares its sequence/counts with observed execution. This is not an independent subagent. The compiler intervention and input generator remain C-only, with no new dependency.

Move the candidate self-host check after the bounded grid and representative probes. Bound each full-suite/self-host command to 180 seconds and preserve timeout/error status rather than calling incomplete checks green. No performance claim is made from elapsed time or any tool/run timestamps.
