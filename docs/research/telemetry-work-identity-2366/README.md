# Telemetry work identity proof — #2366

This receipt preserves the hosted native-harness evidence for draft PR [#2373](https://github.com/buster14a/buster/pull/2373). It is an evidence branch, not an integration candidate. The production change checks telemetry output bytes and source-function counts against the timing sample of the same variant, alongside the existing hash/source-byte/source-line checks. Different consistent A/B artifacts and unavailable function counts of zero remain valid.

## Exact revisions and hosted execution

- Baseline main: `f73f75ed4a59594eb1f43ec505c100457bc35185`; tree `1341478ecae192193c537a064f3050914dc33ec3`.
- Fixed source: `4c08ec62d1ff2ce492031e6151b63ea63d7f948c`; tree `9e72bf78403048a1e164790595d3fede5992c3ec`. Only throughput.c, tests.c and README.md changed.
- Proof transport: `e465fb05ac2c9f28b3c7ff73db37ccdb4e30c290`; tree `74fa2a2096dbf67c9c8927692f6ff27b819ad465`. Its sole delta from the fixed source is the branch-locked workflow preserved here as `proof-workflow.yml`.
- Successful hosted run: [37027722154](https://github.com/buster14a/buster/actions/runs/37027722154), job `110906508700`, Ubuntu 26.04, 2026-10-02 UTC. Observed producer: Ubuntu clang 21.1.8 (6ubuntu1), target x86_64-pc-linux-gnu. Full version, host, command, input and executable hash receipts are inside the archive.
- Original artifact: [11236575361](https://github.com/buster14a/buster/actions/runs/37027722154/artifacts/11236575361), `telemetry-2366-proof`. It expires on 2026-11-01; the exact archive is retained in Git here.

## Result

| Evidence | Baseline with candidate tests | Corrected normal / ASan + UBSan |
| --- | --- | --- |
| Eight independently resealed PMU/allocation mismatches | All eight incorrectly accepted; 24 expected regression assertions failed; suite exit 1 | All eight refused with compare exit 2; both stale summaries absent |
| Consistent different A/B artifacts | Accepted | Accepted |
| Unavailable source-function counts of zero | Accepted | Accepted |
| Six sealed evidence files per case | Preserved | Preserved |

The verifier inspected 30 cases: ten each for baseline, corrected normal and corrected sanitized. Each case has 80 timing rows and 12 telemetry rows. Every negative case changes exactly one allowed byte/function-count field at the final repeat of one variant and one telemetry kind. The independent verifier checked the mismatch location, report presence, and all six sealed-file SHA-256 hashes: 180 file checks. Seal verification detects later mutation; it does not itself enforce semantic identity.

`proof.zip` is exactly 100382 bytes, has 250 ZIP entries, and SHA-256 `a8b2d2fb03a37b3feb75401bbcf0dce4347ebf0db4cdec617e22636a147421b0`. The create-blob receipt is `7348c5ada4dca40733f8a24a9951c36d0f945f32`. The completed hosted [retention run 37030645384](https://github.com/buster14a/buster/actions/runs/37030645384) independently checked that digest, ZIP CRCs and entry count before transfer. Git file readback with base64 encoding then matched every transferred byte. `hosted-job.log` is the original completed proof job log. The archive contains the raw case bundles, completion seals, baseline/normal/sanitized logs and verifier receipts.

## Reproduction and limits

Use the pinned workflow on its explicitly named hosted proof branch. It checks the transport delta, transplants only the new regression tests onto the baseline, requires the baseline failure, then builds the fixed ordinary and sanitized native harness and verifies the resulting bundles independently. The complete commands and sanitizer settings are in `proof-workflow.yml` and the job log. No local, laptop or physical runner execution was performed or authorized for this work.

This is synthetic harness correctness evidence, not a GCC/Clang/MSVC throughput comparison, external-project execution, physical-machine performance verdict, primary-debug qualification or hardware admission. Native Linux/Windows PR harness jobs also passed; macOS harness and real-source qualification were skipped for the draft, and Apple deferred placeholders do not establish Apple execution. Source PR #2373 remains draft with auto-merge off. The integration coordinator retains the unreleased #2172/#2173 admission hold; no queue, merge or admission is requested.

Root Codex Work Mode new-chat-30 is the source/repair and evidence writer; no successor has accepted ownership. Existing #1931 owns external adapter/process policy; its separate child-accounting diagnostic is not a production fix and does not broaden this PR.

## License provenance

Buster first-party licensing remains unselected, verified in [pinned LICENSES/README.md](https://github.com/buster14a/buster/blob/f73f75ed4a59594eb1f43ec505c100457bc35185/LICENSES/README.md); #621 owns that decision. No external implementation or dependencies were imported. Observed Ubuntu clang version is recorded above. Upstream Clang/LLVM 21.1.8 licenses were independently verified at `2078da43e25a4623cab2d0d60decddf709aaea28`: [clang/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/2078da43e25a4623cab2d0d60decddf709aaea28/clang/LICENSE.TXT) and [llvm/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/2078da43e25a4623cab2d0d60decddf709aaea28/llvm/LICENSE.TXT), Apache-2.0 WITH LLVM-exception, retaining separate third-party/legacy notices. This upstream pin does not prove Ubuntu package build/source identity.
