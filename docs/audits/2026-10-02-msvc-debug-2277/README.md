# Matched Buster/MSVC debug evidence

Read the [corrected audit](../../performance-audits/2026-10-02T140009Z.md) for primary debug/debug measurements, the separate optimization gap, and the optional-transform-off sensitivity control.

- Full original archive: [full-debug-evidence.zip](full-debug-evidence.zip), 7,686,423 bytes; SHA256 `34c1a7bf0d1659b06277e49a24190e2f6a4c4652e8646173f1a353fb7dcace35`.
- [Successful measurement run 37016724916](https://github.com/buster14a/buster/actions/runs/37016724916), artifact 11230635693, executed harness/workflow head 5dafc4dcf94a730007b097254ca700a5cf86acf9. The complete archive retains all 401 original files, including every raw process log, fixture, debug object, executable, PDB and checksum manifest. Its Git copy avoids the Actions 14-day expiration.
- summary.json and identity.json retain human-readable measurements and exact image/compiler/binary identities.
- raw-records.json.gz contains the complete original compile.json, runtime.json and processes.json; JSON structure is unchanged after decompression.
- effective-options.json contains full profile commands. The exact [measurement harness](https://github.com/buster14a/buster/blob/5dafc4dcf94a730007b097254ca700a5cf86acf9/tools/research/msvc_comparison/compare_debug.py) and [workflow](https://github.com/buster14a/buster/blob/5dafc4dcf94a730007b097254ca700a5cf86acf9/.github/workflows/research-msvc-debug-comparison.yml) are pinned separately.
- kernels.c, tiny.c, functions256.c and caller.c are identical benchmark inputs/common caller from the cloud run.
- process-0000.log and process-0001.log retain MSVC /Bv and Clang version identities. process-0110.log through process-0117.log retain disassembly and LLVM CodeView decoding for all four final kernel objects. process-0269.log retains the deliberately wrong callee's 108 mismatches and expected exit 3.
- sha256.json is the original full-archive manifest, not a manifest of this smaller unpacked directory. The report and gzip were generated after download and are not entries in it.

Source pin: Buster d4ca72fb4e97ae0a410b0d03258b00495fa45e13. MSVC compiler 19.51.36260.0, installed toolset 14.51.36231. New host is Intel, unlike the old AMD -g0 experiment; do not compare their times across runs.

Both main tiers use the **same** ordinary Buster -O0 -g FAST policy. MSVC /Od /Ob0 /Z7 is primary; MSVC /O2 /Ob2 /Z7 is the separate gap. The four Buster opt-outs remain a labeled sensitivity control. Metadata presence/decoding is not proof of interactive debugger usability. No compiler memory/CPU winner or hardware acceptance is claimed.

Licenses: Buster first-party grant missing/unresolved ([pinned inventory](https://github.com/buster14a/buster/blob/d4ca72fb4e97ae0a410b0d03258b00495fa45e13/LICENSES/README.md), #621); MSVC proprietary [Microsoft 2026 Build Tools terms](https://visualstudio.microsoft.com/license-terms/vs2026-ga-diagnostic-buildtools/); Clang/LLVM 20.1.8 Apache-2.0 WITH LLVM-exception ([version-tag license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/llvm/LICENSE.TXT)). No external implementation source was imported.

Live ownership: [#2277](https://github.com/buster14a/buster/issues/2277), under [comparison umbrella #2334](https://github.com/buster14a/buster/issues/2334). No production compiler change or native-retirement authority change is included.
