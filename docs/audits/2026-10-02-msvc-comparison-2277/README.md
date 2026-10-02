# Buster versus MSVC: immutable diagnostic evidence

The [complete 48-dimension comparison and audit](../../performance-audits/2026-10-02T122303Z.md) includes measured winners, limitations, licenses and ranked improvement proposals.

Buster source: d4ca72fb4e97ae0a410b0d03258b00495fa45e13. MSVC compiler 19.51.36260.0; installed toolset folder 14.51.36231. [Successful cloud run](https://github.com/buster14a/buster/actions/runs/37004619610), [full artifact](https://github.com/buster14a/buster/actions/runs/37004619610/artifacts/11225611157). Artifact ZIP SHA256: adf6caa8113e285e7fbf41440725ad7cbd451cdfe217856053b182427788d8e3. The Actions ZIP expires after 14 days; these selected files remain Git objects.

- summary.json contains medians, ranges and identities.
- compile.json and runtime.json retain every compile/runtime observation including warmups.
- processes.json records all 204 subprocesses, argv, status, wall time and timeout fields. CPU/RSS fields are unmeasured.
- identity.json and process-0000.log/process-0001.log retain image and actual compiler versions.
- kernels.c, tiny.c, functions256.c and caller.c are the exact generated common input/caller fixtures.
- The twelve *-7.obj files are the final trial objects for all three inputs and all four compiler variants. Text hashes were stable across trials. caller.obj is the common independent MSVC caller.
- buster-O0.obj and buster-O3.obj witness identical kernel text under those native option spellings.
- buster-fast-disassembly.txt and msvc-O2-disassembly.txt use LLVM 23.1.1. msvc-O2-disassembly-relocations.txt additionally shows __isa_available checks associated with guarded SSE4.1 paths.
- process-0203.log retains the deliberately incorrect callee's semantic rejection (exit 3 and MISMATCH).
- sha256.json is the original full-artifact file manifest. It lists more files than this selected permanent set; disassembly/audit files were added after download and are not entries in that original manifest.

The [harness](https://github.com/buster14a/buster/blob/791462a112aa0a8410d9e650ad32a91dd0211015/tools/research/msvc_comparison/compare.py) and [workflow](https://github.com/buster14a/buster/blob/791462a112aa0a8410d9e650ad32a91dd0211015/.github/workflows/research-msvc-comparison.yml) are pinned separately at the exact executed revision.

Licenses: Buster first-party grant is missing/unresolved ([pinned inventory](https://github.com/buster14a/buster/blob/d4ca72fb4e97ae0a410b0d03258b00495fa45e13/LICENSES/README.md), issue #621); MSVC is proprietary under [Microsoft Visual Studio 2026 Build Tools terms](https://visualstudio.microsoft.com/license-terms/vs2026-ga-diagnostic-buildtools/); Clang/LLVM version-tag licenses are Apache-2.0 WITH LLVM-exception ([20.1.8](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/llvm/LICENSE.TXT), [23.1.1](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/llvm/LICENSE.TXT)). No external implementation code is imported.

This is diagnostic cloud evidence, not a qualified protected-host acceptance result. No production compiler fix or native-retirement policy change is part of this evidence commit. The live report owner is [#2277](https://github.com/buster14a/buster/issues/2277); leaf and reduction follow-ups are [#2298](https://github.com/buster14a/buster/issues/2298) and [#2299](https://github.com/buster14a/buster/issues/2299).

