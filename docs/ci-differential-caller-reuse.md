# Differential caller-object reuse: implementation and acceptance

Prepared 2026-09-10 for [#408](https://github.com/buster14a/buster/issues/408), slice A. This is a CI-harness work-elimination candidate, not an accepted compiler-throughput improvement. It does not implement case workers or change workflow scheduling.

## Source identity and scope

Initial inspection: `064b2c347fbd29990ce749e76eef0c4b882bc70e`. Publication base: `e2395299585de664602dd081f220d25d524760d6`. Both have `tools/differential.c` blob `cd70885422cbba283bbbefac8aba1a93eee2dd9e`. Candidate blob: `19f8f32467ca360e3c4e4c866571a2cac9d6a40a`.

`d_case_run` prepares a fixed ABI observer once, after its independent O0/O2 references agree and before row scratch checkpoints. `d_prepare_caller` creates `caller/caller.o` inside that invocation's exclusively claimed case output. It removes stale output, requires a normal successful compiler exit, rejects sanitizer reports, requires an existing nonempty hashable object, and records command/stdout/stderr plus the object hash, size and sanitizer policy. Failure prevents comparison rows and fails the case; zero executed configurations is not a pass.

`d_caller_arguments` preserves `-O0`, wrapping, aliasing, unsigned-char and include policy when compiling the caller, and sanitizer flags at both compilation and final linking. Every subject still compiles, links and executes independently. Linux retains its final-link `-no-pie`. The object-only link does not recompile the fixed source.

No persistent cache, new dependency, shell orchestration, worker thread, timeout change or optional test is introduced. Corpus discovery and all 432 current configuration rows are unchanged. Source/include inputs must remain unchanged during a run, as for the existing reference/matrix contract; nothing is reused across invocations.

O0/O2 reference programs retain their original independent source-compilation paths. Every reducer invocation explicitly bypasses caller reuse and recompiles the fixed source under the reducer's sanitizer policy. In particular, an unsanitized matrix object cannot leak into a sanitized reduction. `--source`/`--host` use the same per-case path. Rejection and no-host cases do not prepare a caller.

## Observed baseline, not a speedup claim

Successful first-attempt [run 34518726778](https://github.com/buster14a/buster/actions/runs/34518726778), Intel macOS [job 103010636405](https://github.com/buster14a/buster/actions/runs/34518726778/job/103010636405): job 2,499 s; combinations 734 s; modes 141 s; differential 1,430 s; evidence upload 130 s. Job queue was 5 s. This is one observation, not a matched median.

Retained artifact `desktop-macos-x86_64-34518726778-1`, ID `10170341363`, SHA-256 `7f0c6e7c615ca1c095d89fecf99a0380cbf8d1a52491a5b7faab7421848a5b5d`, identifies actual tested checkout `3c05e9f2ba3121d13bfba3d1a688bbf812cf9bf9`. It is not the PR head or the publication base. Independently summing its `processes.tsv` gives 7,378 compile observations / 249.936569 s, 3,456 link observations / 537.309286 s, and 6,510 run observations / 631.187357 s. The 1,418.433212 s total nearly fills the differential step. Launch, loader, runtime and sanitizer costs are included; this is not compiler CPU time.

Eight fixed host sources appear 432 times each in the saved link commands. This candidate changes their Buster-row source compilations from 3,456 to 8 for that corpus. There are still 3,456 distinct links, and eight separate preparation invocations are added. It does not remove 3,448 child processes or all 537 link-phase seconds. Hosted wall-time and aggregate-cost gains remain unmeasured.

## Validation performed before publication

The full repository could not be cloned in this environment because network/DNS access failed. The baseline file was reconstructed from linked GitHub reads and verified against its Git blob. Testing extracted the exact changed production functions, including the real case runner and reducer, into a Linux-only test adapter. Its arena/file/process/hash support is a stand-in for Buster's runtime. A Clang-backed flag adapter stands in for the Buster subject compiler. Consequently these are runner-plumbing and native ABI link controls, NOT full Buster compiler or native OS-layer acceptance. The adapter's hash implementation is not the production Buster hash.

- All eight retained ABI fixtures passed paired old/new source-vs-object link controls with sanitizers off and on: 16 matched fixture/policy pairs, four comparison rows per invocation, 128 comparison executions and 64 independently built reference executions across both variants. Saved arguments verify one prepared caller per candidate case, four distinct links, and independent source-based O0/O2 references. Stdout, stderr and exit observations agree.
- Added native self-test assertions cover exact compile/link argument policy, paths with spaces, failed/missing caller output, evidence failure, sanitizer reports, abnormal statuses and stale-object removal. The new assertions also passed in the extracted test adapter.
- Caller compile nonzero exit, success without output, crash, timeout and recovering-sanitizer-report controls all failed the case with zero comparison rows.
- A deliberate subject mismatch remained a failure and the extracted real reducer confirmed it. Starting from an unsanitized matrix, every reduction caller link used the fixed source with sanitizers, never the prepared object.
- The extracted runner compiled with Clang 17.0.0 and GCC 14.2.0 under `-Wall -Wextra -Werror` (unused extracted functions excepted). A Clang ASan/UBSan build with recovery disabled passed the focused aggregate fixture. This does not mean GCC was the reference compiler in all paired tests.
- `git apply --check` and whitespace-error checking passed against the byte-exact baseline. The remote candidate blob matches the locally tested blob.

## Required native acceptance before merge

Run the existing build-driver self-test and unchanged full differential command from a complete checkout, with the usual hosted Clang bootstrap:

```sh
work=$(mktemp -d)
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o "$work/buster-build"
"$work/buster-build" test_differential --self-test
CFLAGS=-Wno-invalid-feature-combination "$work/buster-build" generate --cc clang --config Release --linker DEFAULT
"$work/buster-build" build --config Release -t ide
"$work/buster-build" test_differential --ide build/Release/ide --out "$work/differential" --sanitize-oracle
```

`generate` destroys its selected build directory: no other build may be using it. Preserve the ordinary registered tests, self-host, modes, sanitizer and platform gates. Verify the exact complete case/configuration/observation sets, custom source/host and rejection behavior, reduction transitions and evidence-write failures with the actual Buster process layer. Linux/macOS x86-64 and AArch64 acceptance, supported Windows invocation behavior, and full current-head CI remain pending at preparation time.

Collect at least three equivalent successful before/after CI observations per variant, retaining failed attempts separately. Identify actual checkout SHA, caller/subject corpus, configuration registry, compiler/runner image, cache state, queue, critical-path time, aggregate runner time and memory. Inspect `caller/compile.argv`, `caller/manifest.txt`, every subject link and the full coverage records. Do not convert incomplete or inconclusive evidence into a measured win; record accepted measurements in a new indexed performance audit.

## Related work

[#410](https://github.com/buster14a/buster/pull/410) moves unchanged Unix modes and differential suites into independent native jobs, addressing a separate serial dependency under #333. Its files do not overlap this change. #408 slice B owns bounded case workers; #409 owns lossless diagnostic packaging, which must target the new `native-*` artifacts after #410. #335 owns effective coverage and #92 analyzer sharding. None is automatically closed by this slice.
