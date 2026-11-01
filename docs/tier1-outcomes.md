# Tier 1 failure outcomes

The C driver's `--tier1` path keeps a `CheckTier1Outcome` for each attempted
body. It is a pointer-free value, not the former stage/operation bit packing.
`c_check_tier1_outcome.c` defines the contract; `c_check_tier1.c` classifies
returned errors and `c_check_program_emit.c` applies the disposition.

## Policy

| Category | Ordinary `--tier1` | `--tier1 --tier1-strict` |
| --- | --- | --- |
| Compiled | Publish Tier 1 code. | Publish Tier 1 code. |
| Unsupported | Compile that body with Tier 0. | Fail qualification. |
| Resource failure | Fail; no implicit Tier 0 retry. | Fail. |
| Internal / invalid intermediate | Fail with the original cause. | Fail with the original cause. |

The all-zero outcome means **unattempted**, not compiled. Unsupported means an
explicit capability refusal: a naked body, an IR SIMD boundary, unsupported
selection/emission, or unavailable target forms. Invalid IR, allocation
constraints and inconsistent frame/publication metadata are not capability
refusals. Unknown error values fail closed as internal failures.

Memory, matrix/size limits and frame overflow retain their original enum
values as resource failures. There is currently **no Tier 1 resource-retry
policy**. Existing whole-emission retries for lane startup or other phases'
capacity remain separate. They cannot erase an already observed terminal
Tier 1 outcome. Outcomes are cleared before an allowed whole-emission retry,
so an unreached body cannot inherit an earlier attempt's success.

Strict mode requires `--tier1`, applies to every selected/emitted body, and is
propagated to every translation unit. A strict failure writes no new object
or executable. As with other driver errors, it does not delete a pre-existing
output file. Qualification of a particular function should use a fixture
whose emitted bodies are all required to pass strict mode, or assert that
body's outcome directly; native equality alone does not establish coverage.

## Causes, locations and rollback

The outcome retains the stage, category, disposition, cause domain, original
32-bit error, instruction and detail. IR boundaries preserve the complete
`IrCheck`, including `dominance_checked`. The optional `--tier1-gvn` pass retains its
check under the distinct `gvn` stage and follows the same failure policy; it
remains off by default. A lowering instruction is a typed
entry; later IR checks use their API's IR instruction. `UINT32_MAX` means the
API supplied no instruction. These are not source tokens: diagnostics also
retain the body's source declaration, rather than pretending to map an IR
index back to a token.

Selection keeps its refused operation separately. The emitter's optional
`Tier1EmitDiagnostic` preserves nested liveness, IR CFG, allocator and frame
errors before its public error wraps them; the outcome keeps both codes.
Neither the diagnostic nor the refused result borrows an arena pointer.

Failure restores the publication arena, clears code/count outputs, and
rewinds the lane's intermediate arenas. An incomplete reservation is released.
The lane can then compile another function. Publication/frame-adapter
inconsistency is internal; it is not reported as an unsupported emitter.
The existing frame representation and production probe policy are unchanged.

## Reports and ordering

`--tier1-report` retains its stage totals and legacy bounded histograms and
adds `requested`, `admitted`, `compiled`, `unsupported`, `resource_failures`,
`resource_retries`, `internal_failures` and `fallbacks`. Requested is the number
of selected bodies. Admitted excludes explicit naked-body refusals. Resource
retries is zero under the policy above.

Successful emissions report every body. A fatal Tier 1 failure reports only
the source-order prefix ending with that failure: later speculative lane work
is not coverage. A failure of an earlier phase publishes no Tier 1 success
counts. This makes coverage independent of worker completion order; a refused
body never contributes success counters. The source-order failure selection
and lane-completion protocol remain the emission's existing ones.

`--tier1-refusals` gives each permitted fallback's category, cause, original
error/location, operation and disposition. Multi-unit refusals use each unit's
buffered diagnostic stream, so concurrent units cannot interleave them.
Fatal failures go through the normal source diagnostic. Histogram bounds
never truncate the complete per-body outcome or alias unknown errors modulo
256.

## Regression and remaining qualification

Run the focused original C regression through the existing lane command:

```sh
./build/compiler test-check-lanes --tier1-outcomes
```

The ordinary `test-check-lanes` inventory also includes it. It checks raw
unsupported/resource/internal handoff failures, large untruncated error and
location values, real publication exhaustion, lane reuse after refusal,
strict CLI and multi-unit propagation, independent native results, explicit
Tier 1 coverage, and deterministic objects/dispositions on both paths and
1/2/3/8 lanes. Delayed workers exercise out-of-order completion.
`CheckTier1Fault` is an immutable caller-owned regression control; there is
no production environment variable or command-line fault switch.

This implements the failure-contract and strict-qualification slices of
[#1054](https://github.com/buster14a/buster/issues/1054). It deliberately adds
no new input-node scan or early-admission optimization. The issue's measured
early-admission work and locked-9700X performance acceptance remain separate;
this change makes no throughput or memory-performance claim. Analysis
ownership (#1055) and tier-neutral code publication (#1056) retain their
separate ownership.
