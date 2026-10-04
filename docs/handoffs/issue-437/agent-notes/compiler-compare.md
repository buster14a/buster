# Compiler comparison WIP handoff

Stopped at the parent's request for user-directed handoff. No further source work or parent-upload review was started. No host, forge, push or merge changes were made by this agent.

Checkout: `work/compiler-compare` (isolated Git worktree).
Branch: `codex/437-compiler-compare`.
Base: `f4d14d1a6eaf0fcab36ee002d7f458963402a9c0`.
Later, at the user's explicit request to publish all meaningful work, the exact unchanged WIP was committed as `03345bfc84a3c99fa029d9365c7ec730dacb840c`, followed by generated source-size acknowledgment `ee63d4d78608120f68339852a70cec350ff0ee6e` (current HEAD). The working tree is clean. The source hash inventory exactly matches the original precommit WIP. No implementation fixes or new tests were made during checkpointing. Source-size check against the base passes with build growth +62,342 bytes explicitly acknowledged. The implementation remains incomplete: do not admit or deploy it yet. Parent owns serialized GitHub publication.
Ownership claim posted by parent: https://github.com/buster14a/buster/issues/1190#issuecomment-5983926162

## Agreed contracts

Recovery_gap and client_gap both confirmed these reservations:
- Recipe `compiler-compare-v1`, enum 9.
- Broker selector 6.
- Compare stages 33–48 reserved; draft uses 33–37 (two builds, two correctness helpers, throughput).
- New request journal generation 6, shared with runtime/custom C. Historical worker transition threshold remains 3. Native threshold 4 comes from the parent's native integration. Assigned off-host import ledger generation 7 can carry generation-6 compare requests.
- Existing submit-recipe with two lowercase 40-hex revisions; no new public operation.
- Operator-installed source inventories remain required on the worker. Off-host private transfer currently moves native CAS only. Do not claim arbitrary SHA source availability, hosted acceptance, performance qualification or retirement admission.

## Draft source changes

Modified tracked files: build.c; tools/bench_service/{credential_gate.c,protocol.c,queue.c,queue.h,systemd_broker.c,worker_linux.c,workspace.c,zen5_recipe.c}; tools/throughput/throughput.c.
New files: tools/bench_service/{compiler_compare_build.c,compiler_compare_profile.h,compiler_compare_recipe.c,compiler_compare_stage.h,profiles/compiler-compare-v1.recipe}; tools/throughput/evidence.h.

`evidence.h` supplies optional nine anonymous CLOEXEC memfd control/report streams. It makes the collector nondumpable before compiler children, uses the existing native tp_run/tp_compare implementation, seals originals after replay, and emits ordered bounded `TP-PROTECTED-V1` stdout frames. The intended stdout pipe is captured into service-owned result storage; candidate children get diagnostic stdout/stderr and cannot inherit evidence descriptors after exec. Filesystem outputs are diagnostics. Ordinary CLI behavior remains the default.

The shared stage contract binds baseline/candidate source inventories read-only to the same `compare/source` path for serial subject builds. Both write the same `compare/build` path. The trusted build helper runs fixed direct Clang unity compile/link argv; it never executes revision CMake or build scripts. It performs dependency-only preprocessing, hashes header closure before/after, records exact top-level indexed compile/dependency/link argv, Clang/resource path, actual linker --trace inputs, Clang/linker/ldd hashes and resolved dynamic runtime libraries before/after. Fixed generated inputs are operator-installed manifest-listed data under `source/generated`; no revision generator is run. The draft declares a headless Release, tests-on, no-LTO/no-instrumentation profile rather than claiming arbitrary CMake equivalence.

The recipe freezes both subjects before candidate stages, runs untimed subject tests with a trusted helper-owned stdout receipt, calls existing tp_run for six CI workloads/all modes/two warmups/20 pairs per each of two rounds plus frozen-baseline selfhost fixed point, imports original control streams into service-private storage, independently replays native tp_compare in a separate replay directory, compares derived summary hash, and publishes a recipe-specific manifest/bundle. Guard exit 1 is a completed comparison recording a confirmed regression, not an execution error.

Queue/worker/broker/gate registry seams and cleanup names were extended. The existing zen5 child runner was factored through a direct stdout/stderr-separate helper while retaining its original merged-stream wrapper. Its bounded bundle walker was generalized with a recipe-name parameter, retaining the zen5 wrapper.

Source-manifest buffer is now bounded 2 MiB on heap rather than 64 KiB stack. Existing count (4096), per-file (64 MiB), total (512 MiB), directory/path caps remain. Measurement: relevant tracked src/tests/include/LICENSES/CMakeLists inventory has 1,009 files and needs 106,390 manifest bytes, with zero paths over 192 bytes. No complete materialization test above 64 KiB was added yet.

## Completed local evidence (regression, not new-recipe acceptance)

- `work/compiler-compare-throughput-tests.log`: ordinary harness native integration suite 122,930 assertions / 0 failures after evidence-sink changes. Existing native self-test 30 / 0.
- `work/compiler-compare-native-tests.log`: service registered suite 29,599 assertions / 0 failures after initial registry/schema/stage/worker seams. Existing smoke and zen5 driver tests pass.
- `work/compiler-compare-broker-tests.log`: existing broker/gate tests passed after initial compare contract additions.
- Build driver compiled the latest compare source while running the throughput suite, but the actual fixed-Clang compare build and protected run/import were never exercised.
- No running build/test processes remain. Sessions 23910, 30924, 16446 and 18170 completed with exit 0; final process snapshot found only the inspection command.

Earlier independent tasks are complete and remain in root work/: `mcp-final-blackbox-results.json` (old six-tool MCP, 224 scenarios/1082 checks, all pass); Windows stack diagnostics under `windows-stack-diagnostics/`; `native-program-final-blackbox-results.json` (998/998, binary SHA 8adaecdec8688bd926aac637993a909a480aa70e04b6177ff5a3b958472cd032) and `native-program-final-review.json` (no findings with containment/live-host boundaries). These do not cover the parent's new nine-tool native upload MCP changes.

## Known incomplete seams / next steps

1. **Broker installed stage-program identity seam needs completion:** `bq_broker_stage_program` at systemd_broker.c ~973 was not extended for compare. It currently handles zen5 and smoke; new compare request/argv construction is present, but program/first-argument identity must use `compiler_compare_stage.h` too. Add exact broker/gate construction tests for all five stages, ownership, matched read-only bind paths, runtime/FSIZE bounds, selector mismatch, recovery signals and reserved-stage rejection before claiming service readiness.
2. **Add meaningful protected evidence tests:** descriptor CLOEXEC and /proc parent-access denial after nondumpable; deterministic candidate filesystem counterfeit writes cannot replace originals; sealing rejects writes; import rejects wrong count/order/hash/truncation/trailing bytes/size bounds/status mismatch; native replay verifies original fixture records and rejects tamper. Existing throughput tests include a benign fake compiler that copies its own executable for selfhost fixtures, suitable for a complete protected fixed-profile integration run. No new tests were written yet.
3. **Validate fixed subject build on a disposable fixture and actual Buster inventory.** Check the dependency-argv indices and Make dependency parser, generated closure, linker trace format, ldd closure parsing (static trusted tools currently fail closed), complete tool/runtime/header provenance, finite file caps and matched path equivalence. Actual subject binary cap is 64 MiB; verify it fits tests-on Buster. Tool-runtime hash cap is 512 MiB per file / 2 GiB aggregate. Additional tool version/internal Clang argv evidence may be needed; review completeness before claiming complete toolchain provenance.
4. **Closure stability after candidate stages is not implemented yet:** header hashes and tool/runtime closure are verified before/after each subject build, frozen compiler hashes again after candidate stages, but revalidate all original recorded non-source tool/linker/runtime resources after collection. The outer sees an empty compare/source mountpoint outside a stage; source closure paths need deliberate variant-aware mapping to actual immutable base/source or candidate/source, or verified inventory receipts. Do not infer post-build resource stability from two build-stage hashes alone.
5. **Correctness helper needs real fixture validation.** It runs tests from the matched read-only source directory; most Linux fixtures use /tmp, but confirm the full suite doesn't require writable source/build paths. Record both helper receipts explicitly in the final manifest if needed, preserve useful test diagnostics, and add missing/forged/mismatched receipt and failed test cases. Draft parses exact stdout receipt and selected binary digest after each correctness stage.
6. **Protected stdout edge:** native tp_compare routes its main result line to stderr when protected, but optional GITHUB_ACTIONS warning/error annotations still use stdout. Route those consistently to stderr in protected mode or refuse the relevant environment; otherwise they contaminate framed stdout. Gate production clears that environment, but opt-in CLI tests should cover it.
7. **Small WIP cleanup:** compare_reset_build currently lists runtime.before/runtime.after twice (harmless ENOENT handling; remove duplication). Compare profile hash isn't included in success manifest yet; export receipt derives it from the registry, but bind the exact profile in recipe evidence. Memfd wrapper lacks a per-write file cap; fixed configuration bounds the output algorithmically and emit checks size, but independently review/enforce production finite-resource promises.
8. Add durable schema-1–3 compatibility / compare schema-6 admission/refusal tests and complete recipe-specific worker result bind/export/replay/negative-manifest tests. Existing regressions only prove old routes still pass.
9. Complete docs/operator inventory instructions with honest source availability, generated closure, fixed build scope, strict evidence boundaries and no hosted/retirement qualification claim.
10. Run fresh service, broker/gate and harness native + sanitizer checks after changes. Commit, then run source_size on committed revisions and add explicit acknowledgments if the source category exceeds its 32 KiB growth allowance. The new build policy and orchestration were split into separate files to keep each below 32 KiB, but the category growth still requires its normal ratchet handling.
11. Request an independent reviewer for all compare source before parent shared-source integration. This agent's own source is not independently reviewed. Parent owns integration, forge, deployment and live host acceptance. The parent's uploaded-program MCP review request (commit 6ba567d10 + ack73df2f78e) was deliberately not started after handoff instruction.

Scratch source-writing scripts in root work/ (`write_compare_evidence.py`, `write_compare_registry.py`, `write_compare_broker.py`, `patch_compare_core.py`) are historical edit aids, not authoritative or idempotent. Do not rerun them over the WIP. The working tree is authoritative. A tracked-file patch and exact file-hash inventory accompany this handoff.
