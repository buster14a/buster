# Producer / generation / classification experiment

Status at creation: **frozen design; no executed result yet**. This is an isolated research packet, not a production fix or admission change. Owner for these new paths: this GPT-6 Astra Pro investigation. No existing writer branch or production source is modified.

## Source and supported contract

Compiler source is exactly `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree `4c5306221fdb22fccc929b55e333163742de17d0`. Research transport commits are not compiler source commits. The hosted workflow creates separate detached worktrees and checks both identities and an empty tracked diff.

Current AGENTS.md, docs/agents/build.md, docs/agents/testing.md, docs/self-host-audit.md, docs/agents/frontend/foundations.md, docs/agents/frontend/wide-floats-assembly.md, and the implementing build.c were read. The current source's ordinary paths are `build/self-host/Debug/ide-stage1` and `ide-stage2` (not old flat ide-self paths).

`build.c` owns configuration, builds and ordinary self-host orchestration. Its hosted Clang driver bootstrap exception is used unchanged. Both application producers use supported unsanitized **Debug, split-TU** builds with the existing required flags, same host/CPU and explicit default linker. This deliberately isolates producer rather than conflating GCC Debug with Clang Release. GCC's ordinary combination-matrix admission is compile-only; executing its output here is a research observation, not a claim that current CI already admits GCC execution/self-host.

The ordinary native self-host command is invoked for each producer. It builds C0, then C1 and C2 from the full pinned unity source, requires C1/C2 executable-byte equality on Linux, validates metrics, and retains its existing alternate-backend checks. Source-side self-host definitions/flags are not changed. In particular this does not inject BUSTER_OPTIMIZE=1 into the unity source (known separate #1325).

## Minimal matrix and observations, fixed before execution

- Trusted producer: Clang or GCC, matched Debug/split configuration on one authorized GitHub-hosted Ubuntu 26.04 executor.
- Resulting executable: C0 from that producer; C1 and C2 from its ordinary self-host chain.
- Subject target: Linux x86-64 only; native FAST and NONE; O0; frontend SSA; codegen verification. MIR FAST explicitly forbids fallback; NONE does not receive that inapplicable flag.
- Twelve logical subject cells, each executed twice to distinguish repeat nondeterminism: two producers x three generations x two modes. Not a Cartesian product over optimization levels, all targets, all allocators, or all frontend modes.
- Cross-producer comparison is specified behavior, not a binary identity requirement. Within each normal bootstrap chain the repository-required byte fixed point remains mandatory. Probe repeat objects and raw runtime output/status are compared without stripping or normalizing.
- References compile the identical subject using host Clang and GCC at O0/O2. They are linked without LTO to the **same separately trusted-Clang-built observer**.

Record every argv as length-prefixed arguments, status, stdout, stderr, object, executable, and Buster token/IR/MIR trace. The native C research observer only executes bounded subject compiles/links/runs; it does not build Buster or replace build.c. Each small external command is bounded by existing coreutils timeout. Missing stages, compile failures, timeouts, ABI rejection and reference failure remain separate from semantic mismatches. Failed results are always retained.

## Hypothesis and independently established expectations

`c_ir_emit_math_call` in pinned `c_gen.c` narrows generic `isfinite`, `isinf`, and `isinf_sign` inputs to binary64 before classifying them (roughly lines 15664-15770). This is a source-backed hypothesis, not an executed finding at packet creation. A finite x87 number outside the double exponent range may become infinity, so all three queries can consistently misclassify it. The same function implements this loss in every self-host generation; byte stability does not supply an independent semantic oracle.

The observer forms 17 valid x87 encodings from integer significands/exponents using memcpy, checks the actual ABI, and derives classifications from those integer fields. It never invokes Buster's parser/converter or uses a Buster-built observer. Finite exponent fields are not the all-ones special field. Infinity has the all-ones exponent and the explicit integer bit as its only significand bit; the chosen quiet NaNs have additional fraction bits. Zero, subnormal, normal, double-max, positive/negative 2^1024 and 2^4000, x87 maxima, infinities and quiet NaNs are distinguished. No source cast of an out-of-range long double to double occurs in the witness.

The expected Boolean sense of isfinite/isinf/isnan/signbit is tested, not an unspecified nonzero return representation. isinf_sign must give -1/+1 only for the corresponding actual infinity. The public primary contract is GCC's Floating-Point Format Builtins manual and the C classification-macro semantic-type rule; the experiment's x87 representation assumptions are explicit and guarded.

The corpus has 247 independently specified checks per successful profile: 17 x87 images through pointer and by-value classifiers, a representation-preserving diagnostic control, a user-style finite-data guard; three direct-literal reductions; and binary32/binary64 controls. The pointer route separates classification from by-value ABI transport. `image_finite` is a test-only alternate operation, not an installed repair or a proposed portability layer. The static-literal route separately exercises Buster's literal machinery. signbit is the already representation-preserving builtin control. Full raw values are retained.

## Boundaries and novelty checks

No production default, generated binding, canonical IR contract, arena lifetime, lane model, dependency, ownership record, or #36/#880/#881-related policy is changed. No merge, deployment, desktop execution, ad-hoc SSH or benchmark claim. Bootstrap's existing bench command is merely an unchanged correctness gate; its timings are not performance evidence.

All-state issue and PR searches for isfinite found no matching report at design time. Existing #1225, #1226, #1229, #1236, #1294 and #1325 were screened and excluded as new discoveries. Further matching searches and current-source confirmation are required before final publication. The native-width formatting fallback was inspected as a different producer lead; it is not claimed to be a defect.

No callable subagent facility was available after discovery. Source review, oracle authorship and interpretation are serial work by one GPT-6 Astra Pro, not an independent-agent review. Independent compiler/reference and integer-oracle evidence must not be described as independent agent authorship.

Unrun at creation: every experiment, full registered suite, enhanced three-generation/repeated full-source audit, foreign targets, and performance. A green result would narrow this hypothesis, not prove absence of common-mode failures.
