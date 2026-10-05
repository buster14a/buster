# Frozen native census review for #2610

The [catalogue](ci-checks-native-census-d69.json) is a closed inventory of
complete native censuses at source
`d69fd7e8e8fe88e1c668da2c045a2d9d36eca446`, source tree
`f0f22ddcc875b4e1b3560f77a5faf50034243849`, and CI workflow blob
`fdbff4014f7ed96f1244598591e9659cfd14b554`. It supports the prospectively
approved standard-hosted population design in
[#2610](https://github.com/buster14a/buster/issues/2610#issuecomment-5979305216).
The existing exact-nine qualifier remains unchanged. The catalogue does not
accept a performance result or authorize dispatch.

## Archived inputs and authentication

These are the two authentic first-attempt archives from the original
epoch, retained as diagnostic fixtures. Neither sample belongs to a replacement
epoch's timing population.

| Slot | Workflow run | Original sample manifest | Manifest SHA-256 |
| --- | --- | --- | --- |
| A1 | [37191738110](https://github.com/buster14a/buster/actions/runs/37191738110) | `sample-37191738110.json` | `72d8eebf74d0d0e4682054099aa7d909d6f2002b3b1d618657707a2942b8fe88` |
| B1 | [37193669465](https://github.com/buster14a/buster/actions/runs/37193669465) | `sample-37193669465.json` | `cb847a846d42145c1a7c8fb7a065721bfe4f4ee96e86d1936b439dc800765c99` |

Each was freshly processed with the accepted
`ci_checks_qualification.sample(archive_root, manifest,
ci_checks_qualification.PROSPECTIVE_COHORT)`. The parser and every imported
repository dependency were byte-verified against the frozen source before
use. The catalogue records their file/blob digests, both original manifest
objects, and the exact producer blob map. Original archive bytes were not
changed.

This replay checks the complete 21-job inventories and their required steps,
conditions and retained tool/selection receipts; source/run/attempt/job joins;
coverage policy rows and exact union; replayed native phase journals and
summaries; every runtime row's manifest; and the exact native observation
sidecar. Each sidecar binds the same-binary independent `inventory.log` query,
`test.log`, binary hash, command, source/run identity and complete capture.
Module indices, enabled/disabled table audits, external tests, assertion
counts, passed/failed/status fields and measured native profile are retained.
SHA-256 authenticates retained bytes within these joins; it does not itself
prove their external origin. The origin claim is the retained actual Actions
run/artifact custody, not a newly constructed API record.

Five platform identities and all selected policy rows agree. The two samples
have 13 runtime configurations each. Deduplicating only identical full census
objects gives 17 catalogue records: Linux AArch64 3, Linux x86-64 6, Windows
AArch64 1, Windows x86-64 4, and macOS AArch64 3. No inventory, feature word,
module count or assertion was invented. The full census comparison reproduces
the original 33 scalar-field differences; this remains an incomparable pair
under the existing qualifier.

## Closed observed profiles

The catalogue stores all four actual unsigned native feature words and both
compiled SIMD flags inside every exact row census. It admits only an observed
profile in its observed runtime configuration, not every combination of a
known CPU profile and configuration. The same words on different platforms do
not establish the same test workload.

| Platform/profile | Native feature words, in query order | SIMD base/full | Observed configurations |
| --- | --- | --- | --- |
| Linux AArch64 | `[1024,18014398509481984,0,0]` | false/false | Sanitized Debug, sanitized Release, canonical Release |
| Windows AArch64 | `[1024,18014398509481984,0,0]` | false/false | Canonical Release |
| macOS AArch64 | `[1024,18446708889337462784,1023,0]` | false/false | Sanitized Debug, sanitized Release, canonical Release |
| Linux x86-64 lower | `[2305843073638266887,1941315797025,2048,0]` | false/false | All three runtime configurations |
| Linux x86-64 higher checks | `[2305843076388092447,1941315797025,2048,0]` | true/true | Sanitized Debug and Release |
| Linux x86-64 higher release | `[2305913445400705567,1941315797025,2048,0]` | true/true | Canonical Release |
| Windows x86-64 lower | `[2305843073638266887,1941315797024,2048,0]` | false/false | All three runtime configurations |
| Windows x86-64 higher release | `[66637527583,4140334858272,2048,0]` | true/true | Canonical Release |

`test_native_host_profile()` queries x86 features through CPUID with OS/XCR0
usability gates and records the running binary's compiled SIMD flags. AArch64
uses the explicit native target oracle. The feature enum's ordinal minus one
is the storage bit: AVX2 is word-0 bit 2; AVX512F, BW, DQ, VBMI and VBMI2 are
bits 3, 9, 17, 21 and 22. Every admitted x86 profile has AVX2; the lower profiles
lack all five named AVX-512 features and the higher profiles have them. CPU
model names and assertion totals are not used to infer these facts.

## Source review at the frozen revision

The older [source accounting](https://github.com/buster14a/buster/issues/2120#issuecomment-5951676609)
was anchored at `e424b387fcb51b00c1d19e87e2d369ae8a212792`. This review retrieved
both revisions from Git, independently extracted the named complete function
regions with strings/comments excluded from brace matching, and compared their
original bytes. The catalogue binds each reviewed source file/blob and each
region's SHA-256. The following are byte-identical between those two revisions:

- Both `compiler_driver_test_wide_vector_boundaries()` and
  `compiler_driver_test_native_frame_vectors()`.
- `machine_test_win64_vector()` and its execution and two host-vector helpers;
  `machine_test_predicate_source()`, `_widths()`, `_edges()` and `_bank()`;
  and the stage-10 vector source and native-execution region.
- `ir_simd_operation_supported()`, the feature-bit indexing/containment
  functions and the relevant Win64 host-ABI guard.
- The complete `simd_test.c` and `simd.h` files.

Whole driver and machine test files are not unchanged. For example, intervening
changes add driver output/link/assembly/variadic/wasm fixtures and machine
frame-certification/variadic/switch fixtures. Other changed test modules include
frontend C, assembly, codegen, IR, link, object, bitcode, OS and string tests.
Consequently the old absolute assertion counts are not the expected counts at
the frozen source. The catalogue takes its complete current baselines from the
authenticated pair, then checks the explained profile differences against the
current feature-gated regions.

### Driver obligation

[`compiler_driver_test_wide_vector_boundaries()`](https://github.com/buster14a/buster/blob/d69fd7e8e8fe88e1c668da2c045a2d9d36eca446/src/buster/tests/compiler/driver/driver_test.c#L5331)
executes its znver5 rows when
`ir_simd_operation_supported(target_native, IR_SIMD_SPLAT_BYTE)` is true. That
operation requires usable native AVX512F and AVX512BW. On successful Linux
execution the newly admitted rows add:

- 72: 3 allocators × 2 frontends × 2 relocation forms × 3 fixtures × 2
  link/run assertions.
- 8: 4 padded-vector allocators × the selected frontend/relocation form × 2
  link/run assertions.
- 66: 2 Clang exchange directions × (1 host compilation assertion + 4
  allocators × 2 frontends × 4 compile/link/run assertions).

[`compiler_driver_test_native_frame_vectors()`](https://github.com/buster14a/buster/blob/d69fd7e8e8fe88e1c668da2c045a2d9d36eca446/src/buster/tests/compiler/driver/driver_test.c#L5897)
adds 96: 4 allocators × 2 frontends × 2 relocation forms × 3 full-vector
fixtures × 2 link/run assertions. These fixtures use the same F/BW execution
predicate. Link caching does not remove the per-cell assertions.

The driver obligation is therefore +242 on Linux. Windows omits the Unix-only
72 and 66 paths, retaining +104 = 8 + 96. These paths are not omitted by
sanitization. The authentic pair directly verifies +242 in all three Linux
runtime rows and +104 in Windows canonical Release. It contains no observed
higher-profile Windows sanitized row; that otherwise source-explained
configuration is not fabricated or admitted by this catalogue.

### Machine obligation

Native machine execution is guarded by `!BUSTER_SANITIZE`. The Win64 vector
host-ABI gate additionally requires x86-64, Clang and a non-self-hosted binary;
the catalogue's runtime rows use the actual admitted Clang capabilities.
[`machine_test_win64_vector()`](https://github.com/buster14a/buster/blob/d69fd7e8e8fe88e1c668da2c045a2d9d36eca446/src/buster/tests/compiler/codegen/machine_test.c#L6619)
and `_execute()` require usable AVX512F/BW and add 3672 on either OS:
2 target systems × 2 memory forms × (132 fixed assertions + 3 variadic modes ×
262 assertions). A fixed execution has 2 entry assertions + 2 names ×
(1 offset assertion + 8 seeds × 8 lane assertions); the variadic form has
4 names instead of 2.

Non-Windows unsanitized execution additionally requires:

| Region | Host feature predicate | Additional successful assertions |
| --- | --- | ---: |
| `machine_test_predicate_source` | F/BW | `2 memory forms × 4 modes × (2 entry checks + 8 seeds × (1 guard + 64 lanes + 1 word)) = 4240` |
| `machine_test_predicate_widths` | F/BW/DQ | `4 widths × 4 inputs × 3 modes × 2 checks = 96` |
| `machine_test_predicate_edges` | F/BW | `3 variants × 3 modes × 2 checks = 18` |
| `machine_test_predicate_bank` | F/BW | `4 call variants × 3 modes × 2 checks = 24` |
| Stage-10 vector differential | F/BW/VBMI/VBMI2 | `4 × (3 entry checks + 5 internal-call relocation checks) + 3 modes × 4 masks × (6 calls × 2 checks + table + stores) = 200` |

The stage-10 source has five internal direct call sites, and the native
differential retains the relocation checks and all table/store/call checks.
The authenticated complete module delta independently agrees with the sum;
this review did not run a new compiler to inspect generated relocation tables.
The canonical obligation is +8250 = 3672 + 4240 + 96 + 18 + 24 + 200 on Linux,
and +3672 on Windows. Sanitized machine counts remain equal. Source predicates
are not collapsed to one generic AVX-512 capability; every admitted profile
satisfies its specific predicates.

### SIMD obligation and complete-module comparison

[`simd_test.c`](https://github.com/buster14a/buster/blob/d69fd7e8e8fe88e1c668da2c045a2d9d36eca446/src/buster/tests/simd_test.c#L407)
adds one malformed quarter-selector assertion only under
`#if !BUSTER_SIMD_512`. The full compiled SIMD path is x86, non-MSVC and requires
compiler AVX512F/BW/VBMI/VBMI2 macros. Its obligation is therefore -1 assertion
for the observed higher compiled-SIMD profiles.

The independently authenticated current absolute counts are:

| Runtime row | Driver lower / higher | Machine lower / higher | SIMD lower / higher |
| --- | ---: | ---: | ---: |
| Linux sanitized Debug | 50285 / 50527 | 513508 / 513508 | 157 / 156 |
| Linux sanitized Release | 50285 / 50527 | 513508 / 513508 | 157 / 156 |
| Linux canonical Release | 55278 / 55520 | 584776 / 593026 | 157 / 156 |
| Windows sanitized Debug | 46539 / unobserved | 513508 / unobserved | 157 / unobserved |
| Windows sanitized Release | 46539 / unobserved | 513508 / unobserved | 157 / unobserved |
| Windows canonical Release | 48300 / 48404 | 514916 / 518588 | 157 / 156 |

Across both archives, all other complete module assertion records match for
each runtime row. Independent inventories, skipped table-audit sets and
external-test populations also match. Every retained module reports all
assertions passed, zero failed and `pass` status. There is no per-assertion
normalization or removal of the profile fields.

## Limits and validation

The catalogue is intentionally finite. An unseen native word, different
compiled SIMD flag, unknown row/profile combination, unexplained count or
source/workflow/producer/condition drift stops a replacement epoch as pending
or inconclusive. A future extension needs new independent diagnostic evidence
and source review before a new declaration; replacement timing samples cannot
teach this catalogue their own expected counts. This pair establishes neither
CPU-model affinity nor every future hosted-image/profile population.

Validation is offline: both complete accepted sample replays, equality of all
platform identities/selected rows, exact catalogue-to-replay census equality,
source and imported-reader byte checks, reviewed-region comparisons, JSON
round-trip and documentation whitespace/link checks. No compiler, native test
or Actions workflow was launched. Resource/deadline/capture/cleanup/reliability
review and the prospective 36-slot campaign remain separate gates.

License: Buster's first-party license is unspecified/unselected at the frozen
source, as recorded in
[`LICENSES/README.md`](https://github.com/buster14a/buster/blob/d69fd7e8e8fe88e1c668da2c045a2d9d36eca446/LICENSES/README.md)
and [#621](https://github.com/buster14a/buster/issues/621). This review adds no
external implementation or dependency.
