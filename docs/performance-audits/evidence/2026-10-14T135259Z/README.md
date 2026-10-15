# Direct-row decoder CI repair evidence

Implementation: `c37abc202fc490cc0c2a06cfa8f809ef23114f2b`. Previous immutable
[storage](../2026-10-14T125135Z/README.md) and
[SDK guard](../2026-10-14T131054Z/README.md) bundles retain their original identities.

`identities.json` records matching baseline/candidate production compile
arguments, frozen executable/output hashes, section sizes, semantic symbols
and the exact source repair. `storage-identity.json` independently reconstructs
the original five payloads and checks 385 packing cases plus deterministic
re-emission. `a64-output-identity.json` records all four object comparisons.
`raw-evidence.tar.gz` contains completed validation and hosted failure logs;
its `partial/` directory explicitly contains in-progress local runs at this
freeze. `SHA256SUMS` covers all bundle files except itself.

Reproduce with the preceding bundles' matched native build, SDK and frozen
input commands, substituting this implementation. Additional repair checks:

```sh
./build.sh build --config Release -t test_all
./build.sh build --build-directory build/sanitize -t test_all
./build.sh test_self_host --build-directory build/native --config Release
./build.sh test_mode_matrix --build-directory build/native --config Release
TSAN_OPTIONS=halt_on_error=1 BUSTER_TEST_JOBS=4 taskset -c 3,5,7,9 build/tsan/Release/ide test --ci=1 --verbose=0 --module=aarch64_semantics_tests,aarch64_semantic_vm_tests,aarch64_system_semantics_tests,aarch64_memory_semantics_tests
./build.sh build --build-directory build/tsan --config Release -t test_tsan
./build.sh clang_analyze build/analyzer-tree --config Release --shards 8 --jobs 2 --results build/analyze-ci-fix
```

These commands reuse separately configured owned trees. Only generate an idle
fresh tree, using the sanitizer/TSan/canonical analyzer configurations in the
[agent instructions](../../../../AGENTS.md) and
[analyzer guide](../../../clang-analyze-shards.md). The audit records which
commands had completed; later PR/issue comments record final local and hosted
results without changing this bundle. Hosted Clang 21 remains authoritative
for reproduction of its original warning; local Clang 23 did not warn before
the repair.

Local size/output evidence is Linux/Ryzen 9 7940HS, with no speed, RSS or Zen 5
claim. Buster first-party licensing is [unselected](../../../../LICENSES/README.md);
existing Arm projections retain their [permission-review boundary](../../../../THIRD_PARTY_NOTICES.md#arm-xml-inputs-and-derived-projections-permission-review-remains-open).
No new upstream material is added.
