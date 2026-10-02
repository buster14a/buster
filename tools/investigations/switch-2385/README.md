# SWITCH singleton-key evidence for #2385

Frozen research result, 2026-10-02. The [owning issue](https://github.com/buster14a/buster/issues/2385) carries current lifecycle and ownership; this directory is an immutable evidence snapshot, not a progress ledger.

## Result and decision

Validated canonical IR produces different observable branches across native and LLVM consumers. On both hosted x86-64 and AArch64, an unsigned 8-bit ARGUMENT holding 7 with a singleton SWITCH raw key 263 returns the default value 22 in NONE, MIR_STACK, FAST and QUALITY; LLVM returns the case value 11. Disassembly shows native consumers materialize 263 and compare 32 bits. LLVM compares the selector's 8-bit image against 7.

The disagreement is reproduced. Calling a particular branch a language miscompile still requires the canonical singleton-key spelling contract to be explicit. The predeclared diagnostic oracle is equality modulo selector width: truncate_w(selector) == truncate_w(key). All reported "match"/"mismatch" counts refer to that oracle.

**Recommendation:** define SWITCH equality on the selector-width bit pattern, normalize keys and selector representations consistently in each consumer, and retain normalized duplicate-key rejection. This agrees with LLVM's typed constants, the eBPF source implementation, and the equivalence relation used by pending #2384. A validator-only alternative must explicitly reject out-of-width singleton spellings before every consumer, define accepted signed spellings, and reconcile the duplicate-key contract. Do not silently choose singleton semantics through #2384's uniqueness check.

Immediate gate: canonical-IR maintainers decide and document that contract; no successor has accepted implementation ownership. After that decision, the bounded consumer repair and validation contract below are ready. This research creates no production repair claim or merge request.

## Identities and scope

- Production source: main commit `7afde682de1beb595c779192c9519b7524afb412`, tree `b1a3545713f54e390902b4e9a90fdb5b10bc608e`.
- Diagnostic source: [`b925d3cb6b11769b55a8e1beb0867cdd8127381d`](https://github.com/buster14a/buster/commit/b925d3cb6b11769b55a8e1beb0867cdd8127381d), tree `44e1e7abb7a3b281b84f7984a9c02879ae270b8e`, branch `codex/2385-switch-key-evidence`.
- [Compare against production pin](https://github.com/buster14a/buster/compare/7afde682de1beb595c779192c9519b7524afb412...b925d3cb6b11769b55a8e1beb0867cdd8127381d): only the probe workflow, a 5-line codegen-test hook, the 121-line C fixture and the 52-line independent observer differ. Production compiler sources are unchanged.
- [Hosted run 37036971037](https://github.com/buster14a/buster/actions/runs/37036971037), x86-64 job 110937460354; AArch64 job 110937460624. Standard ubuntu-26.04 and ubuntu-26.04-arm runners, sequential jobs, Clang 21.1.8 (6ubuntu1), unity Release, two build jobs and one test job.
- Main advanced during collection to `353c338398173120a36bbe18221d6ce032ec4cd3`. Its diff changes assembly and offsetof paths, not the validator, native SWITCH consumers, LLVM, Wasm or eBPF inspected here. The C frontend diff only touches offsetof logic. No execution result is relabeled as a run on that later revision.
- Pending [#2384](https://github.com/buster14a/buster/pull/2384), head `5632078ec5b6246bbdda7afe4758a4532e05d40b`, retains ownership of duplicate normalized keys (#2378). It masks for uniqueness but leaves singleton raw payloads untouched. All-state SWITCH/raw-key issue and PR searches were refreshed at handoff; no competing singleton repair was found.

The final evidence commit removes the diagnostic workflow and fixture hook from branch tip, restoring the original codegen test blob. The executable probe remains available at its immutable run commit. No production compiler source is changed or proposed for merge.

## Experiment

Each cell freshly preprocesses, parses and lowers a one-argument C skeleton:

```c
int choose(TYPE x) {
    switch (x) { case 7: return 11; default: return 22; }
}
```

The fixture then changes only the SWITCH operand to the original typed ARGUMENT and its singleton immediate. This deliberately bypasses C integer promotion at the canonical boundary. It checks the exact argument width/signedness, one argument, one switch and one key, and calls `ir_validate_canonical_module` before emission. Each consumer receives a fresh module.

Widths 8/16/32/64 × signed/unsigned × 5 variants × 5 consumers = 200 cells per architecture, 400 total:

| Variant | Raw key | Observer input | Expected under declared oracle |
| --- | --- | --- | --- |
| 0 | 7 | 7 | 11 |
| 1 | 2^width + 7; 7 at width 64 | 7 | 11 |
| 2 | 2^width - 1 | maximum unsigned value or signed -1 | 11 |
| 3 | UINT64_MAX | maximum unsigned value or signed -1 | 11 |
| 4 | 8 | 7 | 22 |

Consumers 0/1/2/3 are NONE/MIR_STACK/FAST/QUALITY with invariant verification and fallback recording; consumer 4 is Buster LLVM bitcode consumed by host Clang. An independently compiled Clang C caller invokes each artifact, prints its result, and records link exit, process exit, stdout/stderr and artifact SHA-256. Expected results come from Python integer bit-vector arithmetic, not another native mode.

The 64-bit alias variant necessarily repeats key 7; variants 2 and 3 coincide at width 64. Counts are declared probe cells, not 400 distinct semantic shapes.

## Observed outcomes

| Target | Validated | Executed | Oracle matches | Oracle mismatches | Refused emission |
| --- | ---: | ---: | ---: | ---: | ---: |
| x86-64 | 200 | 192 | 164 | 28 | 8 |
| AArch64 | 200 | 200 | 168 | 32 | 0 |
| Total | 400 | 392 | 332 | 60 | 8 |

All 160 positive/default controls and all 80 LLVM cells match. Those populations overlap and must not be added. All 392 executions linked successfully, returned exit 0 and had empty stderr. Every one of the 320 native emission records reports fallback count 0.

- On both targets, width 8/16, both signednesses, variant 1 disagrees in all four native modes: 16 cells per target.
- Variant 3 additionally disagrees in all native AArch64 modes (16 cells) and the three x86-64 MIR modes (12 cells).
- x86-64 NONE refuses variant 3 at widths 8/16/32 for both signednesses, and variant 1 at width 32 for both signednesses: 8 cells, `CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION` (2), zero generated bytes, no artifact.
- All sampled 32-bit MIR, AArch64 and LLVM cells, and all 64-bit cells, match.
- Per-mode x86-64 results: NONE 28 match / 4 mismatch / 8 refused; each MIR mode 32 / 8 / 0; LLVM 40 / 0 / 0. AArch64: each native mode 32 / 8 / 0; LLVM 40 / 0 / 0.

Native modes agree with each other on the simplest failing witness. Allocator differential testing alone would miss this semantic inconsistency.

The x86-64 job is red: 11947/11955 codegen assertions passed; eight emission-success assertions failed, followed by the observer's completeness assertion. Results were written before that assertion. Its subsequent control/LLVM assertions were not reached, so they are independently checked by the offline census. The AArch64 collection job is green with 11935/11935 codegen assertions and complete observation; its observer intentionally permits recorded mismatches. **Green collection is not a production correctness pass.**

Raw printf output and test-runner messages interleave on x86-64. The offline verifier removes only explicitly recognized complete runner messages, reconstructs all 200 validation and 200 emission records per target, checks their unique Cartesian identities and key/error/fallback fields, and leaves the original logs untouched. It also recalculates the oracle and verifies all 392 emitted artifact hashes against the downloaded ZIPs. No downloaded executable is run locally.

## Source attribution

All links in this table use the production pin.

| Boundary | Source and observation | Evidence |
| --- | --- | --- |
| Canonical validation | [ir.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/ir/ir.c), SWITCH check near 6064: integer selector, shape/targets/result, no singleton key-width restriction | Source + 400 successful explicit validations |
| C producer | [c_gen.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/frontend/c/c_gen.c), `c_ir_switch_promoted_type`, `c_ir_terminate_switch`, lowering near 39020: promote selector, convert/mask cases | Source; ordinary C-source miscompile not established |
| Native MIR | [machine_x86_64.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/codegen/machine_x86_64.c), `machine_x64_select_switch`; [machine_aarch64.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/codegen/machine_aarch64.c), `machine_a64_select_switch`: carry raw key, choose 32/64-bit comparison | Source + independent execution + disassembly |
| Native direct/NONE | [codegen.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/codegen/codegen.c), SWITCH branches near 19918 and 23762: same widened comparison; x86 uses metadata immediate emission | Source + independent execution/refusal + disassembly |
| LLVM | [bitcode.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/llvm/bitcode.c), SWITCH near 4455 and `llvm_bc_encode_integer_bits`: selector-typed constant reduced to declared width | Source + 80 independently consumed outputs |
| Wasm | [wasm.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/wasm/wasm.c), `wasm64_fe_emit_switch`: narrows raw key to i32/i64 representation, not exact narrow selector width | Source-only candidate for analogous disagreement; not executed |
| eBPF | [ebpf.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/ebpf/ebpf.c), `ebpf_fe_emit_switch`, `ebpf_fe_normalize`, `ebpf_integer_image`: explicitly forms matching selector/key images | Source only; no new eBPF runtime or kernel claim |
| SPIR-V | [spirv.c](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/src/buster/lib/compiler/spirv/spirv.c), instruction whitelist | SWITCH rejected; no supported-runtime claim |

The [LLVM switch definition](https://llvm.org/docs/LangRef.html#switch-instruction) uses selector-typed integer constants and equality. It corroborates LLVM's behavior, but does not decide Buster's currently unspecified raw spelling contract.

## Bounded implementation and acceptance contract

1. Record the singleton-key rule in canonical IR documentation/validation tests; coordinate with #2384. Prefer modulo-width equality. Duplicate normalized keys remain invalid, and a singleton alias remains valid under that choice.
2. Reuse existing integer-width/image helpers where suitable. Repair both native MIR selectors and both direct emitters at the existing SWITCH boundary; handle the 64-bit mask without an undefined shift. Ensure the operand image and key image agree, including negative signed inputs. Masking keys alone is insufficient if a producer leaves a differently extended operand.
3. Add small permanent C regressions built from canonical ARGUMENT/SWITCH shapes, including input 7/key 263, signed -1 with low-width and all-u64-one spellings, width-32 high aliases, and in-range/default controls. Preserve zero-fallback coverage across the four native modes and both architectures. Include an independent semantic oracle; do not merely compare allocators.
4. Obtain the small Wasm runtime witness before asserting that backend repaired; retain eBPF/LLVM comparisons as independent boundaries. Do not add a compiler pass or optimization to solve a local representation contract.
5. On the repair's exact revision, rerun the declared matrix: under the recommended contract, all 400 cells must emit and match; under an explicitly chosen rejection contract, every rejected spelling must fail canonical validation consistently before consumers. Run the repository's required self-host fixed point, affected test_all and allocator matrix gates. None of those production-repair gates is claimed by this diagnostic run.

This is correctness discovery, not a performance experiment. No throughput, speedup, benchmark acceptance, production incidence, physical-host result, Windows/macOS result or full unsupported-width coverage is claimed. Main baseline CI/fixed-point evidence already existed; this task does not substitute a new full baseline run for it. Integration holds #2172/#2173 and existing backend ownership remain untouched.

## Retained evidence and reproduction

[Probe C](https://github.com/buster14a/buster/blob/b925d3cb6b11769b55a8e1beb0867cdd8127381d/src/buster/tests/compiler/codegen/switch_key_probe.c), [observer](https://github.com/buster14a/buster/blob/b925d3cb6b11769b55a8e1beb0867cdd8127381d/tools/investigations/switch-2385/observe.py), and [exact hosted workflow/commands](https://github.com/buster14a/buster/blob/b925d3cb6b11769b55a8e1beb0867cdd8127381d/.github/workflows/switch-key-research-2385.yml) are immutable. Reproduction is hosted correctness only; preserve the selected revision and tool versions.

The two initial setup failures remain visible: [run 37036507303](https://github.com/buster14a/buster/actions/runs/37036507303), head b4c54a54, shallow checkout lacked the parent for source diff (no compiler run); [run 37036708181](https://github.com/buster14a/buster/actions/runs/37036708181), head 5cefdcab, Clang rejected two fixture String8 pointer casts (no Buster runtime result). Corrections were confined to the probe. The observed failures in the third run were retained without retrying for a green result.

| Target | Original hosted artifact | ZIP SHA-256 |
| --- | --- | --- |
| x86-64 | [11241006514](https://github.com/buster14a/buster/actions/runs/37036971037/artifacts/11241006514), 648344 bytes | `66af6925660f69bc18403dfd45e883c7092bcd9d971b3ef766e4c8fba23b1831` |
| AArch64 | [11240468101](https://github.com/buster14a/buster/actions/runs/37036971037/artifacts/11240468101), 838081 bytes | `ea8f84f6b01a4e757e601cfe19e57226dd93552724aff7d304db0d64d9131fe4` |

Original artifacts include generated objects/bitcode and executables and expire on 2026-11-01 under 30-day retention. No generated build outputs are committed as source. This directory durably retains text-only evidence archives: all raw build/probe/observer/link logs, results, observer sources, binary identities and a SHA-256 manifest for every original ZIP member. It also retains offline disassembly, the verified 400-cell summary and the reproducible offline verifier.

- [x86-64 text archive](evidence/x86_64-text.tar.xz), 23884 bytes, SHA-256 `f39fc22eb78d97b9f3c93e6ef766ef43725ea7ab66b15ca93e35691856e2c906`.
- [AArch64 text archive](evidence/aarch64-text.tar.xz), 24336 bytes, SHA-256 `116522854a5b89301a78c458b61de3d0a8f3d2867f842f7a4d58e216c27657da`.
- [Verification summary](evidence/verification-summary.json), [x86-64 disassembly](evidence/x86_64-disassembly.txt), [AArch64 disassembly](evidence/aarch64-disassembly.txt).
- [Offline verifier](verify_evidence.py): `python3 verify_evidence.py x86_64.zip aarch64.zip --output verification`. It verifies the ZIPs without executing any artifact; selected binary files are extracted for optional read-only disassembly. No compiler or downloaded binary execution is performed by this verifier.

License/provenance: Buster's first-party license remains unresolved under #621, verified from [LICENSES/README.md at the production pin](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/LICENSES/README.md). LLVM is Apache-2.0 WITH LLVM-exception, verified from the retained upstream [llvm-LICENSE.txt](https://github.com/buster14a/buster/blob/7afde682de1beb595c779192c9519b7524afb412/LICENSES/llvm-LICENSE.txt); component-specific exceptions remain in that document. No external implementation, dependency or vendored code was added.
