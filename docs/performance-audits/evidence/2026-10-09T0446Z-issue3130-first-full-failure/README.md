# First full analyzer comparison: failed attempt

This bundle retains the byte-exact original artifact ZIP inside
`first-attempt.zip.xz`. Decompressing it yields the 6,887,111-byte artifact
whose SHA-256 is
`2af7199739e60509946562aa9ac233541808cda1401425beb444390a703f27b0`.
The original archive contains 422 files, including all ten phase captures,
both preparation manifests, four analysis and independent aggregate captures,
baseline per-TU logs/results, candidate manifests, provenance and receipt.
`raw-file-sha256.json` identifies every original member without changing it.
`failure-observations.json` derives diagnostic observations from those raw
captures. It is not an acceptance receipt.

The automatic owner-authorized request run was 37884110155. Compare run
[37884120234](https://github.com/buster14a/buster/actions/runs/37884120234),
attempt 1, compared baseline driver
`6badc05b250b18a3e76e1de396480a60373774fa` with candidate driver
`14736f0092f03191bf9ea2022ec2e758c7b19745`. Both analyzed the same candidate
source and shared Release database. The request digest and receipt identity
were independently checked against GitHub's exact request/head/base records.
Artifact 11595239346 was downloaded after the failed compare job; the
independent publisher successfully published failure.

The baseline analyzed all 182 selected rows twice, retaining the same ten
Clang 23.1.1 `core.NullPointerArithm` diagnostics across seven failing rows.
The candidate planned 135 executions and 47 aliases, but all eight workers
rejected its manifest in each repeat before TU execution. Its embedded
1,057,099-byte database exceeded the generic 1 MiB string-reader limit. The
candidate's early-abort timings cannot be compared with baseline coverage as
a speedup. Performance qualification remains incomplete.

All four raw `ANALYZE_RUN` observations have complete process-tree sampling,
positive samples, live-process counts and sampled RSS. The original derived
summary dropped these observations when failed aggregate parsing raised an
error. They remain diagnostic failure evidence here, with comparison invalid.
The native helper attempted all ten phases and reported fail; eight command
phases failed with child exit 1, without timeout, cleanup or capture failure.

Buster's first-party license remains unselected; see
[`LICENSES/README.md`](../../../../LICENSES/README.md) and issue #621. Installed
LLVM/Clang is `Apache-2.0 WITH LLVM-exception`, documented in
[`LICENSES/llvm-LICENSE.txt`](../../../../LICENSES/llvm-LICENSE.txt). No external
implementation or new dependency was imported. This is not a complete package
license audit of the runner.
