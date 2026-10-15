Reproduce the FAST prepass investigation
Baseline: 84f051c13044938bcb8cbe1ac48ea3ee79defcff
Accepted implementation: 0a1e15e6de985987ee19537686a85dba743fbe13
Rejected expanded-analysis experiment: 58a292938da799e8296697f555c07f1e826431f8
Intermediate budget-only implementation: 7f968fa99f9c7d1921059efd688e24570637a4b6

The archive preserves every controlled cohort, not just favorable observations.
Accepted receipts start query-; final-* names refer to the earlier budget-only cohort.
The raw evidence is bound by manifest.json, source Git blobs and binary hashes.
Binaries remain at the recorded local paths and are not included in this archive.

Build trusted compiler arms serially in the same owned source/build path using
./build.sh generate --build-directory build/performance -- -DBUSTER_INCLUDE_TESTS=OFF
./build.sh build --build-directory build/performance --config Release -t ide
Freeze each image before changing sources. Saved cache/compile_commands are identical
between arms; use the full flags and Clang version recorded there.

For probes, restore each pinned revision's src/ under one source/ path, compile the
same recipes/probe.c using probe-builds/builds-query.json's command, and freeze each
image separately. Use the same path for both builds. The initial diagnostic probe
with raw CFG edges is a different workload from this matched public-API probe.

Reconstruct frozen compiler input from baseline src/ and generated headers produced
by that baseline configuration. Verify generated-header hashes in the manifest.
The recipes and input hashes bind each whole-compiler observation. Absolute paths
are recorded deliberately; another build root may change binary identity.

The performance-evidence tool comes from tools/performance_evidence.c at the pinned
revision. Recipes/run_query_probe_evidence.py and run_query_native_evidence.py contain
all final commands. Baseline and candidate alternate, CPU4 affinity; instructions:u
uses six pairs, native time/RSS ten, each after one warmup, minimum effect0.5 percent.
Do not run owned builds or full tests during native timing. All assertions and
unsupported/incomplete samples must stay visible. Native memory is process max RSS,
not the probe's retained arena bytes or a sum of transient planes.

Full tests, self-host, allocator matrix and focused GCC/ASan/UBSan/bounds commands
are preserved by the validation logs and recipes/focused.c. The focused build uses
O1,g,function/data sections, BUSTER_INCLUDE_TESTS=1, BUSTER_LINK_LIBC=1,
BUSTER_OPTIMIZE=1,BUSTER_SINGLE_THREADED=1, the project wrap/alias/unsigned-char flags,
codegen.c,arena.c,os.c,integer.c,string.c,float.c,time.c, gc-sections,lm,pthread,dl;
Clang adds address,undefined,bounds sanitizers. GCC uses Wall,Wextra,Wconversion,
Wshadow,Werror and unused-function/variable suppressions. Tool versions: Clang23.1.1,
GCC16.2.1 20260810. Run these executables and require solver=1 summary=1, exit0.

Buster first-party licensing remains unselected, SPDX NOASSERTION, per the pinned
LICENSES/README.md. No third-party implementation is imported by this change.
