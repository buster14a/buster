# Issue #3130 second full analyzer attempt

This evidence package retains the second approved 9700X full analyzer attempt for PR #3167 and the available pre-campaign and #3168 VA-correction captures. The attempt failed its correctness gate in every arm. Its elapsed times are observations only; no baseline/candidate ratio or performance disposition is valid.

## Bundle and verification

[The lossless TAR.XZ bundle](3130-evidence-bundle.tar.xz) is 7,150,628 bytes with SHA-256 `2a9696a3cfb31ed25cc86be649b7b40b6ef283ca531fe88d952dc8bae0461ff0`. It has 47 regular-file members: 42 payload files plus five metadata files. All payload sizes and SHA-256 values were checked against [bundle-manifest.json](bundle-manifest.json); the copied metadata files match their archive members byte-for-byte. The payload total is 17,393,790 bytes.

[observations.json](observations.json) is the compact row, phase, sampler, and source-correction summary. The full independent facts and the second-attempt audit remain inside the archive at `inputs/second-failed/issue3160-second-campaign-independent-facts.json` and `inputs/second-failed/audit.md`. That nested audit's `archive-members.json` and `../pre-campaign/` links resolve within the archive's `inputs/` tree; they are not links to the scratch staging directory. The first physical ZIP is not duplicated here; it remains in the [first-attempt audit](../../2026-10-09T045046Z.md).

The archive includes the original second-attempt ZIP (6,985,721 bytes, SHA-256 `95086bb50bc6465b2fafd37345d5c443a5d764679043165876cf1069f8a73d23`), its 802-member inventory, all four arm logs, all ten process-phase records and associated logs, pre-campaign request/authorization/TCC material, the H-associated Release log, and the available VA-correction captures. [bundle-provenance.json](bundle-provenance.json) records scope and omissions. `SHA256SUMS` covers every file stored beside this README other than the checksum file itself.

## What the attempt establishes

The exact external identity matched PR #3167 head `7c75f6277c723125c09152a21cffc217c14db5c5`, tree `6f711a0b96409a3c97b343d2614eadf4488d1a5d`, against base `521b791421a962a085a0a9685357b6ea5ca790ef`. On the Ryzen 7 9700X with Clang 23.1.1, both baseline arms selected and executed 182 rows. Both candidate arms selected 182 rows and recorded 135 unique executions plus 47 aliases. All four arms failed on row 97 in `src/buster/lib/compiler/ir/ir.c`; each had 181 passing rows and that one failing row. All four aggregate phases also failed. The comparison is invalid.

The retained warning is `core.NullPointerArithm` at `ir.c:270:31`, on the observed VA_COPY path through `ir_type_from_id`. The trace identifies the failing path; it does not alone establish a general source root cause. The follow-up [#3168 correction](VA-correction-provenance.json) adds a missing-type-table guard for VA operations and a focused regression. The correction's captured validation is bounded: it used Clang 21.1.8, some runs were on pre-commit source bytes, some successful output was console-only, and Clang 21 rejected `core.NullPointerArithm` as unavailable. Those captures do not constitute a corrected 9700X full-run result.

All four `ANALYZE_RUN` records report complete process-tree sampling, but this only describes the sampler. It does not repair the analyzer failure or qualify a performance result. `observations.json` retains the ten process-phase costs and the independent facts distinguish sampled process-tree RSS from `wait4`'s largest individual high-water.

The bundle also contains an H-associated Linux Release job at merge commit `646d7e6a5daba7ae056cec1647b39a3080086ad2`, whose tree matches H. It recorded 141 self-test checks and a local 18-row aggregate; it was not a literal H checkout or the 182-row 9700X campaign. See [H7c75-release-provenance.json](H7c75-release-provenance.json).

## Integrated-source size record

A separate native source-size report was run against local integrated commit `4a5e2e5aa55624eec920244eb0e8ada86aa7d073`, tree `5ad874305f3665e14c1fa61b1c5417091ed4425c`, with selected main `8ea54465a2309f95bcb160143ce4804e05a8dbc8` as the explicit base. The report passed: production growth was 4,992 bytes against a 32,768-byte limit; build growth was 256,070 bytes and was acknowledged by the existing baseline at `7a75db7c21ce7623c27d4f6ea2826ccc83c8bc8f`. The baseline was not refreshed. The report is a source inventory, not a full build or performance test. Its raw output and driver provenance are [source-size-local-4a5e-vs-8ea.log](source-size-local-4a5e-vs-8ea.log), [source-size-driver-provenance-c183.txt](source-size-driver-provenance-c183.txt), and [source-size-provenance.json](source-size-provenance.json). [source-integration-provenance.json](source-integration-provenance.json) distinguishes the local integration identities from the failed H campaign identity.

## License scope

Buster's first-party license grant remains unselected under #621, as documented in [LICENSES/README.md](../../../../LICENSES/README.md); no Buster SPDX identifier is inferred. The installed LLVM/Clang toolchain's retained license is SPDX `Apache-2.0 WITH LLVM-exception` ([LICENSES/llvm-LICENSE.txt](../../../../LICENSES/llvm-LICENSE.txt)). An installed Clang was used; no external implementation was imported. This is not a package-by-package distribution license audit.
