# Issue #59 encoder-run experiment

This directory preserves the temporary diagnostic and rejected prototype used
by [the 2026-09-30 audit](../../../docs/performance-audits/2026-09-30T193624Z.md).
The production encoder source is unchanged by that audit. The patches apply to
the audit's base `a947bc1d0e0050e516743b9a51253645dd805345`; apply only one
prototype patch at a time in a disposable checkout. `instrument.patch.gz` counts
completed x86-64 machine functions. The other patches batch at most 32
consecutive rows of one fixed-width frame form, using either direct scalar
stores or eight-record AVX-512 VBMI2 byte compaction.

From the repository root, configure and validate the unmodified source first:

```sh
./build.sh generate --cc clang
./build.sh test_self_host --config Release
```

For the census, decompress and apply `instrument.patch.gz`, build `ide`, copy the diagnostic
binary somewhere outside `build/`, then restore
`src/buster/lib/compiler/codegen/machine_x86_64.c` **without rebuilding**.
This makes the compiler binary diagnostic while the compiled source remains the
exact base. Run the following command with that copied binary:

```sh
gzip -cd tools/research/x86_encode_runs/instrument.patch.gz | git apply
./build.sh build --config Release -t ide
cp build/Release/ide /absolute/path/to/copied-ide
git restore src/buster/lib/compiler/codegen/machine_x86_64.c
DIAGNOSTIC_IDE=/absolute/path/to/copied-ide
"$DIAGNOSTIC_IDE" cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 \
  -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=native -g0 \
  -fregister-allocator=fast -fverify-codegen -fno-machine-fallback \
  -c src/buster/apps/ide/ide.c -o /absolute/path/to/unity.o \
  > /absolute/path/to/census.stdout 2> /absolute/path/to/census.stderr
python3 tools/research/x86_encode_runs/analyze.py /absolute/path/to/census.stderr
python3 tools/research/x86_encode_runs/analyze.py /absolute/path/to/census.stderr \
  --form-tag ENCODE_RUN_OPCODE
```

`ENCODE_RUN_FORM` is the narrow two-register prepared-table population without
patch fields. `ENCODE_RUN_OPCODE` is a broader upper bound: one exact opcode,
one prepared variant, one emitted instruction, no edit inside the run, and no
new call/fixup. Actual displacement and register bytes can still vary. Both
classifiers close runs at block and placement-edit boundaries; an edit before a
row starts a new run at that row. The raw diagnostic, summaries, and five paired
comparison records are retained beside the audit.

For a batch comparison, build the unmodified baseline and copy its binary.
Decompress and apply one batch patch with `gzip -cd PATCH.gz | git apply`,
rebuild in the **same configured path**, copy that
binary, then restore the source before running either compiler. Both arms
compile the same base source and write a complete object to the same path.
`run_pairs.py` records two warm-up pairs followed by alternating A/B and B/A
pairs; the audit used 15 measured pairs. It retains command lines, raw PMU
output, wall times, statuses, and output hashes, and stops on a byte mismatch.
The runner requires Linux `perf` access for user-mode cycles and instructions.

```sh
python3 tools/research/x86_encode_runs/run_pairs.py \
  --baseline /absolute/path/to/baseline-ide \
  --candidate /absolute/path/to/candidate-ide \
  --output /absolute/path/to/pair-results --repetitions 15 -- \
  cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 \
  -target x86_64-unknown-linux -march=native -g0 \
  -fregister-allocator=fast -fverify-codegen -fno-machine-fallback \
  -c src/buster/apps/ide/ide.c
```

The patches are research snapshots, not source to register in CMake. SIMD
prototype admission was declined, so no AVX2 arm, sanitizer/mode matrix, or
platform matrix was pursued.
