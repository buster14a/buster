# Packed A64 semantic storage evidence

Measured implementation: `59b8fb74f351f404eef297d8829cb430d019c60d`.
Baseline: `69f594c81a52eb3457496d6c12761fda24ffce55`.
Frozen compile input: `e34df042d7a1d4820dd61c3d3ae39aee7e17a261` plus
`inputs/generated/buster/lib/shaders/paths.h`.

- `build-identities.json`: trusted compiler version, matched normalized flags,
  exact compile commands, source-input hashes, CPU/OS, binary hashes, stripped
  sizes and full ELF sections. Executables remain frozen in the session's
  `work/` directory; their hashes, rather than mutable build paths, identify them.
- `storage-verification.json`: independent full-payload byte identities and
  385 deterministic packing cases. Original-payload SHA-256 values are retained.
- `verify-storage.py`: portable independent decoder and deterministic emitter
  verification against the original base64 header. It imports the candidate's
  emitter, not raw Arm XML. Its output is written to the requested separate path.
- `output-identities.json`: byte-identical A64 probe object hashes and sizes;
  the audit records the complete frozen x86-64 linked-output identity.
- `comparisons/`: repository-native comparison summaries, including binary,
  tool, input-argument, hardware and every per-run resource identity.
- `raw-comparisons.tar.gz`: the full comparison directories, including raw
  per-run stdout/stderr (mostly empty), preserved compactly.
- `validation-logs.tar.gz`: exact completed build/test/self-host/matrix,
  source-size and link-check logs; see `validation.json` for dispositions.
- `rejected-*.json`, `once-before-span-screen.json`: exploratory intermediate
  timing arrays. They are diagnostics, not accepted claims or reproducible
  source snapshots; the audit explains their boundaries.

Reconstruct independent baseline and candidate checkouts at the revisions
above. Use separate build directories owned by this reproduction; `generate`
deletes its selected directory and must run only while that tree is idle.
For each checkout, build and freeze the trusted production subject:

```sh
./build.sh generate --no-include-tests --build-directory build/native
./build.sh build --build-directory build/native --config Release -t ide
cp build/native/Release/ide /absolute/path/to/frozen-subject
```

Use the same installed compiler and flags for both. This audit used Clang
23.1.1 on Linux x86-64/Ryzen 9 7940HS with `-march=native`; another compiler or
machine is a new measurement. Sizes can be reproduced with `size`,
`readelf -SW` and `strip --strip-all -o /absolute/path/to/copy SUBJECT`.

Verify storage independently without obtaining any new restricted input:

```sh
python3 verify-storage.py --baseline /absolute/path/to/baseline \
    --candidate /absolute/path/to/candidate --output /absolute/path/to/result.json
```

Create the frozen compile input from `git archive` of the recorded input
revision, then copy the retained `inputs/` contents into its root. Build the
comparison helper from `tools/performance_evidence.c` with trusted Clang
(`-O2 -std=c11 -Isrc -lm`). From that frozen input root, the full comparison is:

```sh
taskset -c 0 /absolute/path/to/performance-evidence compare \
    --claim native-runtime --tool time --runs 40 \
    --baseline /absolute/path/to/frozen-baseline \
    --candidate /absolute/path/to/frozen-candidate --out /absolute/path/to/new-result \
    -- cc -Isrc -Igenerated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 \
    -g src/buster/apps/ide/ide.c -lm -o /absolute/path/to/output
```

For each retained `.s` probe or `storage-tiny.c`, replace the arguments after
`--` with `cc --target=aarch64-linux -c INPUT -o OUTPUT`. The startup diagnostic
uses `help`; its original run was unpinned. Same-binary controls use the
baseline executable as both arms (16 unity pairs, 40 repeated-assembly pairs).
Raw subjects and inputs were not edited during measurements. Keep native
runtime observations separate from the deterministic executable-size result;
these data establish no Zen 5 claim or general compiler throughput improvement.

Buster's first-party license is unselected, as recorded in
[LICENSES/README.md](../../../../LICENSES/README.md). Existing Arm-derived
metadata retains the permission-review boundary in
[THIRD_PARTY_NOTICES.md](../../../../THIRD_PARTY_NOTICES.md).
No raw Arm XML or new third-party material is included in this evidence.
