# Canonical and machine validation boundaries

This contract complements the [frontend guide](agents/frontend/foundations.md),
[machine ownership inventory](machine-metadata-ownership.md), and
[differential testing](differential-testing.md). The focused implementation is
[GitHub #294](https://github.com/buster14a/buster/issues/294), not a replacement
for dense canonical finalization (#38) or machine metadata work (#45).

## A certificate describes one input

`CIRLowerResult.canonical_ir_certified` describes the successful C producer's
output. The `input_certified` argument of `ir_prepare_canonical_module` permits
skipping its **input** scan. Promotion consumes that input proof: it rewrites
instruction/value IDs, block parameters, incoming values and opcode summaries.
The input certificate is not evidence about those rewritten rows.

After actual promotion (`promoted_locals != 0`), preparation invokes the existing
`ir_validate_canonical_module` when the input was uncertified or when any of
these compile-time conditions holds:

- `!BUSTER_OPTIMIZE` (debug/unoptimized build), `BUSTER_INCLUDE_TESTS`, or
  `BUSTER_SANITIZE`;
- the explicitly enabled `BUSTER_VERIFY_IR_TRANSFORMS=1` diagnostic/measurement
  switch.

Setting the explicit switch to zero cannot disable debug, test or sanitizer
checks. An optimized, non-test, non-sanitized production build retains the
existing pass-contract fast path; it trusts the transformation's implementation,
not an assertion that the producer validated its output. Disabled, unchanged
and already prepared certified modules do not gain a redundant output scan.
The existing `-fverify-codegen` option still forces independent input validation
and native MIR/placement checks, including in optimized production builds.

`local_promotion_complete` records successful pass completion, **not** a general
IR certificate. Failed output validation does not publish that marker. A caller
that later mutates prepared IR must discard its certificate and supply
`input_certified=false`; preparation performs input validation even when the
pass-completion marker is already set. A failed result must not be consumed by
code generation or retried with the stale input certificate.

`IrValidationResult.boundary` identifies the last preparation scan, on both
success and failure: `CANONICAL_INPUT` or `LOCAL_PROMOTION_OUTPUT`. Raw verifier
calls and certified preparation that performs no scan return `UNSPECIFIED`.
The field is diagnostic context, not a persistent proof or mutation counter.
The driver reports it alongside the existing error, function, block,
instruction and opcode context. The cold result grows; authoritative canonical
instruction/value rows and machine rows do not change size.

## Existing checks and limits of the evidence

| Boundary / owner | Existing checks reused | What a successful check does not establish |
| --- | --- | --- |
| Canonical input and promotion output / `ir_validate_canonical_module` | Required storage; instruction-chain ownership; block sealing and termination; value and operation types; call/return signatures; parameter/incoming types, counts and predecessor order; branch-target validity; global alignment, initializer and relocation ownership | This change does not add a whole-function canonical dominance proof or prove full CFG predecessor/successor symmetry. Those properties must not be inferred merely from valid IDs and parameter counts. |
| Selected MIR / `machine_verify_function` | Side-table bounds; opcode and operand kinds; register classes and physical limits; fixed/tied constraints; block instruction coverage and terminators; parameter/edge-copy classes; definition counts/points and immutable-register dominance, including edge uses | Opcode-specific payload validation is not an independent proof of every emitted instruction's width/encoding. Legacy explicitly mutable registers retain their separate definition contract. Bounded edge spans alone are not a proof of complete CFG symmetry. |
| Scheduled MIR / native code-generation verification path | With `verify_invariants`, reruns the same machine verifier on the reordered candidate, before replacing the accepted function or using its rebuilt placement | The selector's original certificate cannot certify reordered rows, remapped definition points or the new placement. This slice leaves the existing opt-in scheduled-MIR hook and its counters unchanged. |
| Placement / native code generation | Existing valid-placement checks and strict `verify_invariants` failure handling | Structural IR verification does not prove ABI behavior or generated-program semantics. Use the existing independent compiler/runtime matrix. |
| Relocations / canonical module and object emission | Canonical globals validate label ownership and relocation storage/ranges; MIR validates its local symbol-reference metadata | A standalone machine-function verifier lacks module symbol/definition ownership. Preserve module/object checks rather than treating a local MIR success as relocation certification. |

There is no new permanent use-list representation. Canonical compaction and
machine scheduling must rebuild or remap their derived use/liveness/placement
facts; the ownership inventory records their producer, lifetime and invalidation.
This inventory is source-level reconciliation, not a claim that all remaining
CFG, dominance, width and relocation hypotheses were reproduced or repaired.

## Regression and measurement contract

`ir_promotion_tests` now runs the existing structural corpus with both certified
and uncertified input. It asserts disabled/unchanged/idempotent behavior and the
actual output-check boundary whenever promotion removes rows.
`ir_promotion_validation_tests` deliberately introduces safely backed faults
outside the function that is promoted: global alignment, missing relocation
storage, a data symbol misused as a label owner, an unterminated block, and a
return-type mismatch. Uncertified input must fail before mutation; a deliberately
stale certificate must allow real promotion but then fail at the output check,
with exact contextual IDs and no completion publication. A separate case
revokes the certificate after a later mutation of already prepared IR.
These are negative hook controls, not frontend or promotion miscompilation
reproducers. They use the existing registered IR module, not a parallel harness.

Integration against main also exposed previously certified invalid frontend
output: complex predicates used integer bitwise operations on Boolean values,
and qualified field initializers could capture an unqualified value at a
different field type. `basic_c_ir_validation_values.c` exercises the repairs
with strict native verification/execution in both frontend forms and all four
allocators. The registered IR tests validate the raw producer output across
x86-64, AArch64 and Wasm64 layouts, retain volatile accesses, then prepare it.
Negative Boolean-operation controls reject integer opcodes, non-Boolean results
and place results. The existing aggregate operand/type checks remain intact.

For a negative control, restore only the old post-promotion condition
`!input_certified` in an isolated CI workspace while retaining the new tests and
context. The certified-output assertions must fail. This control is not an
acceptable production configuration and must not replace any required gate.

Measure optimized production builds of the **same source** with tests and
sanitizers disabled, changing only `BUSTER_VERIFY_IR_TRANSFORMS` between zero
and one. Reuse `bench_throughput run`, frozen generated inputs, alternating pairs,
all four allocator modes, and `--require-identical-output`. Include
`--flag -fno-frontend-ssa` so the canonical-promotion workload is not accidentally
measuring an unchanged direct-SSA module. Record binary/input/output hashes,
compiler build flags, host identity, raw samples, CPU/wall time and available
RSS/counters. The ordinary base-versus-head throughput CI gate remains separate.
Build time, compiler implementation throughput and generated-program runtime
are different measurements. This contract does not supply an unrun measurement.
