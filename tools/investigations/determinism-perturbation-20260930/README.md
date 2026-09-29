# Contract-preserving determinism perturbations (#53)

Research only. Source: `8f67df736f13d4edc055110a7a6d619a00a22eaf`, tree `8810c6603e47e5eecf26e1d73633619a347c70d8`. No production source, ordinary CI, admission rule, or persistent-lane policy is changed on this branch. `apply.c` installs four exact-marker edits into a disposable working copy only. It refuses missing/duplicate markers. The working-copy diff is retained. Never run the installer in an integration owner's checkout.

## Predeclared contracts

1. **Semantic observations:** valid native x86-64 Linux programs must exit zero (independent Clang ASan/UBSan oracle plus a hand-derived sum of 116). Invalid source must fail normally and preserve a preexisting output sentinel. AArch64 Linux output is compiled, not executed. Agreement with an unchanged Buster alone is not semantic validation.
2. **Diagnostics:** compare stdout, stderr and compiler exit status separately. Capture every published structured diagnostic in input order: code, severity, symbol, message, primary/original paths and positions, range IDs/offsets/lengths/presence, notes, and backend evidence. Encode individual little-endian fields and length-prefixed strings; never raw structs/pointers/padding. Later completed TUs' errors/warnings may be discarded under the documented first-failing-input rule; their *published* messages may not replace the earlier error.
3. **Artifacts:** the whole linked file must be byte-identical within each fixed target/allocator/debug profile. No stripping, sorting, timestamp patching, path replacement, address removal, or binary normalization. The existing file must remain byte-identical after a rejected compile. Filesystem mtimes, process IDs, arena addresses, elapsed time, active worker counts and internal completion order are telemetry, not artifact bytes.
4. **Paths/time/debug:** source paths, include paths, working directory, source bytes, output path, target and options stay fixed. `-g0` and `-g` are separate reference cells, not equivalent-byte pairs. Thus there is no path/debug/timestamp exception inside a cell. `__FILE__`, `#line`, `__DATE__`, and `__TIME__` appear in fixtures. Current date/time predefines are documented as fixed-epoch definitions; do not assume `SOURCE_DATE_EPOCH` support (repository search found none). Different checkout paths/SDKs, debug modes, producer binaries or non-fixed time inputs are not claimed as tested invariants. Permitted build-environment differences cannot excuse a mismatch in this fixed envelope.

## Mechanisms and reach

The C-only overlay has no new library dependency. Ubuntu's existing C runtime is used for diagnostic I/O and `nanosleep`; normal Buster builds never include the header.

- Request jobs 1/2/3/4 on ten consecutive C link inputs. Record `CompilerDriverResult.compilation_workers` and each actual input slot/lane, rather than equating requested jobs with execution. No `-c` worker-count claim.
- Delay before independent TU execution in opposite lane orders, keeping stable input slots, the persistent gang, all barriers and input-order publication. Atomic start/finish tickets record actual order; a requested delay is not by itself proof of changed completion order.
- Consume 4 KiB-plus of unrelated valid TU-arena storage. Record arena bases and positions before/after. Do not corrupt live bytes or defeat the allocator's zeroing/dirty-position contract. This tests placement, not arbitrary allocator implementations, OOM or exhaustive dirty-memory poisoning.
- Before the subject, optionally compile three independent immutable inputs in the same process: an x86-64 multi-TU link, an AArch64 object, and an expected frontend failure. Release each result arena while retaining the normal thread context and persistent gang. Replay both history orders. Record prior statuses and actual workers. These independent invocations are explicitly nonsemantic to the subject; archive inputs and the subject's input order are never changed.

The inactive overlay is compared with the original build. The first pass compares raw outputs without stage tracing. A second pass sets distinct per-TU prefixes for the existing token/canonical-IR/selected-MIR diagnostic serializers inside the isolated unit kernel. The normal command-line restriction to one traced input is unchanged. Compare complete streams with their completion footers. The traces are the project's existing semantic projections; they exclude full source-recovery maps, physical positions, unused intern-table capacity, lazy ABI caches and post-allocation state. Matching traces do not prove equality of those omitted states.

## Execution

Use a fresh **standard GitHub-hosted Ubuntu runner**, not the desktop. No performance result is intended; the dedicated 9700X is not needed for this correctness experiment.

```sh
bash tools/investigations/determinism-perturbation-20260930/run.sh
```

The script uses the documented hosted Clang bootstrap exception, builds one unchanged Release compiler and one isolated diagnostic compiler with tests disabled, then runs four cells: x86-64 ELF `-g0`, x86-64 ELF `-g`, AArch64 ELF `-g0`, and deterministic invalid-input publication. Every normal object is strict FAST MIR with codegen verification. No fallback/admission gate is relaxed. Exact commands, compiler hashes, overlay, input hashes, raw outputs and diagnostics, reach telemetry, and phase traces go to `det-evidence/`.

Planned rows: 4 cells × (2 original/inactive controls + 4 worker requests × 7 profiles), plus 2 trace rows for each of the 3 valid cells = 126 subject invocations. This is a plan, not an execution claim. Same-process prior calls are additional and reported separately. All expected statuses, required telemetry, trace counts/footers and comparisons must complete. A comparator positive control changes a disposable copy, not measured output. A run with missing telemetry, clamped-to-one workers, unexercised placement or unchanged completion order cannot substantiate those perturbations.

Stop rule: retain any mismatch and compare token, canonical IR and selected MIR before blaming allocation/encoding/publication/serialization. A matching MIR stream narrows the search but does not identify which later layer is responsible. Do not label a binary diff an attributable defect without that next investigation. Otherwise report only this finite envelope, with no claim of universal determinism or whole-language correctness.

## Ownership and prior evidence

- [#53](https://github.com/buster14a/buster/issues/53) owns TU lanes and output/diagnostic invariance. Extend it rather than creating another determinism epic.
- [#1265](https://github.com/buster14a/buster/issues/1265) already records extensive negative perturbation tests on `ade6ac4b...`, plus a distinct arena-lifecycle defect. [#1303](https://github.com/buster14a/buster/pull/1303) owns its TU-arena remedy; [#1545](https://github.com/buster14a/buster/pull/1545) owns preprocessing-lifetime work. Neither repair is imported here.
- [#303](https://github.com/buster14a/buster/issues/303) is closed/completed: previously uninitialized merged-section padding. The current fixture retains alignment gaps as regression pressure; do not report the old defect as new.
- [#424](https://github.com/buster14a/buster/issues/424) owns topology-aware performance measurement. No speedup claim here.
- The self-host audit's existing token/IR/MIR schemas and limitations are respected; fixed-point equality is not substituted for perturbation testing.

Read: `AGENTS.md`, `docs/agents/{parallelism,driver,research,build}.md`, `docs/self-host-audit.md`, driver unit/publication code and diagnostic/source-range schemas at the source anchor. Search included open/closed issues and PRs for determinism and #1265.

## External method and licensing

Current primary guidance: [Reproducible Builds environment variations](https://reproducible-builds.org/docs/env-variations/) and [timestamps](https://reproducible-builds.org/docs/timestamps/), consulted 2026-09-30. Apply environment variation while defining the byte contract first; do not adopt timestamp normalization or a tool merely to make comparisons pass. The [SOURCE_DATE_EPOCH specification](https://reproducible-builds.org/specs/source-date-epoch/) is a contract to verify, not evidence that Buster implements it. No external implementation is copied or vendored.

Buster has **no selected first-party project-wide license**, as explicitly stated in pinned `THIRD_PARTY_NOTICES.md`; #621 owns that decision. Its retained upstream notices do not grant a blanket Buster license. LLVM/Clang is a host tool, not imported by this harness; its retained license is Apache-2.0 WITH LLVM-exception (see `LICENSES/llvm-LICENSE.txt`). No new dependencies are installed.

## Remaining blind spots

Finite small corpus; Linux hosted compiler only; FAST allocator only; at most the hosted runner's actual CPU count; no BUSTER_SINGLE_THREADED build, ASan/TSan build of Buster, Windows/COFF/PDB or macOS/Mach-O output, AArch64 execution, Wasm/GPU/eBPF/LLVM output, self-host stage-1/2 perturbations, arbitrary mid-pass preemption or allocator reuse poisoning, path remapping/checkout relocation, source file mtime changes, OOM/cancellation, full interner/source-identity dumps, post-RA MIR snapshots, or canonical pre/post-link ObjectFile snapshots. The independent Clang oracle does not cover the invalid diagnostic text or AArch64 runtime. Raw evidence, not this README, decides which planned rows actually executed.
