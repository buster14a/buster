# Issue 116 handoff, 2026-10-23

David requested a safe stop and GitHub handoff through the review session.
This session releases its remaining #116 optimization/qualification claim and
the narrow #117 `pp_prepare_into` producer claim. No performance candidate is
selected, no merge is requested, and issue #116 remains open. David explicitly
retained **1.5 ms**, including setup or adjacent-phase costs moved by the change.

## Revisions and ownership

- Draft [PR #334](https://github.com/buster14a/buster/pull/334), branch
  `codex/116-refinement-budget`, remains at
  `74295ce876279f02d7099801dbf758105ae975a8`.
- Its most recent merged main base is
  `2740ff1d59bce335747bdcfd7d9994ce4426c320`. Recheck current main before
  implementation; later main changes are not silently included in this claim.
- This archival branch, `codex/116-handoff-20261023`, preserves the subsequent
  uncommitted experiments and tests without advancing the PR head or launching
  another CI run. The workflow triggers pushes only to main and PR events;
  this branch has no PR.
- Session: `01a1680c-d6df-78a0-9782-46ae090af161`. Checkout:
  `/home/david/Documents/Codex/2026-10-23/ful/work/buster`.
- Claim records:
  [#116](https://github.com/buster14a/buster/issues/116#issuecomment-6361651050)
  and [bounded #117 producer scope](https://github.com/buster14a/buster/issues/117#issuecomment-6370000233).
  The completed #323 owner explicitly cleared the latter. #133's semantic
  pragma/deferred-origin work is separate and remains with that owner.

No local build/test remains active. The manual fourteenth screen was stopped
while its first build waited for the shared host; it acquired no lock and has
no timing result. The twelfth screen had built only `twelfth-lexprecise` and
was stopped in its next lock wait. The thirteenth script was prepared but
never launched. No scheduling window or host reservation remains.

## What is implemented in the PR

Production performance code remains the baseline. The production change is the
intern hash correctness repair: paired changes can cancel in the low qword of
the old 128-bit fold while the differing high qword is discarded. A final AES
round mixes both halves before truncation. Four exact regressions, 8,192 guarded
bucket patterns and eight table-growth boundaries are retained. Full key
comparison already prevented wrong identifier IDs; the repair fixes the hash
quality defect and keeps its existing assertions intact.

Internal table hashes intentionally change. First-seen IDs, keyword IDs,
normalized spellings, physical/literal extents, table formats and the separate
`link_hash` do not. Local seeds 1–128 covered 25.6 million random spellings,
11.27 million structured spellings and 1,898,354 aligned paired flips, with
zero failures after the repair. The old failing control is retained in the
experiment archive rather than presented as passing.

All performance candidates are overlays under `candidates/`, applied to the
frozen baseline with `patch --batch --fuzz=0`. The [README](README.md) records
the complete sequence, commands, negative results and raw target outputs.

## Earlier completed target evidence

[Required run 39990074577](https://github.com/buster14a/buster/actions/runs/39990074577)
passed at head `6fd9529cf85d932979366e5956741233ebd94529`, testing merge
`54c62bb69b633686f891d51bf30c23c345a23c3d`, job 120539311747.
[Raw output](screen-splice-ci-9700x.txt) and
[summary](screen-splice-ci-9700x.tsv) include source/executable hashes and all
169 system-header checks before and after measurement. There were 36
preprocessing processes, nine setup processes and 18 standalone kernel
processes; each normal process reports the existing 21-pass benchmark.

| Candidate | SQLite 3.53.4 refinement min/median/max ms | Whole preprocessing median ms |
| --- | --- | --- |
| Fixed-hash production | 1.720 / 1.729 / 1.731 | 11.851 |
| Precise splice regions, space in columns | 1.468 / 1.473 / 1.485 | 11.694 |
| Precise regions, space in permanent arena | 1.542 / 1.557 / 1.558 | 11.612 |
| Block-level empty-splice skip, permanent arena | 1.519 / 1.519 / 1.534 | 11.530 |

The column-space result moves arena first-touch into directives and does not
establish the charged budget. Permanent-arena variants include that actual
fault in refinement. `blockgap` is still above target on both SQLite inputs
(3.53.2: 1.524 / 1.534 / 1.535 ms). It improves current main.c's whole median
41.728 to 41.516 ms, but regresses the libc standalone kernel from
28.198–28.238 to 29.084–29.214 ms. It is unselected.

The standalone SQLite kernel improves in both measured power states:
1.351–1.356 to 1.294–1.308 ms sustained, and 1.385–1.397 to 1.329–1.337 ms
after 100 ms scalar conditioning. Correctness passed for all four candidates;
`blockgap` also passed 134,784 guarded preparation cases, 1,200 explicit splice
boundaries, allocation failures, shared-file tests and Clang output comparisons.
The standard required CI suite passed afterward.

## Final completed qualification at the handoff

[Run 39992461786](https://github.com/buster14a/buster/actions/runs/39992461786)
was already running for exact PR head `74295ce876279f02d7099801dbf758105ae975a8`
(required job 120547440505) when David requested the handoff. It finished
successfully at 17:23 UTC, testing synthetic merge
`1689f11f3af4aa9e1d7f66ae94b646067636688f` onto main
`ded2a6911051a3f228136fcea188dea1f6924299`. No replacement run was launched;
no owned command or CI run remains in flight. Its script is
[ci-splice-screen.sh](ci-splice-screen.sh), comparing:

- `fixed`: production with the hash fix;
- `coldpref`: `blockgap` plus restored column/source prefetches for the
  whole-file consumer, retaining the prefetch-free cached handoff;
- `directspace`: `blockgap` with permanent leading-space storage grown as
  tokens arrive, eliminating the temporary space array and copy;
- `directcold`: both changes.

The [raw candidate output](screen-direct-storage-ci-9700x.txt),
[preprocessing summary](screen-direct-storage-ci-9700x.tsv) and
[complete job log](screen-direct-storage-ci-job-9700x.txt) preserve the exact
composition and executable hashes. Both SQLite hashes and all 169 system
headers passed before/after checks. All four candidates passed scalar/full
refinement checks on 9,252 files (117,850,310 bytes). `directcold` also passed
134,784 guarded preparations, 1,200 explicit splice boundaries, allocation
failures, shared-file/lane/ID checks and Clang comparisons on compiler main.c
and SQLite. The standard required suite then passed.

The screen contains 36 preprocessing processes, nine setup processes and 18
kernel processes, with three rotating processes per candidate/input and the
existing 21-pass measurements within each process.

| Candidate | SQLite 3.53.4 refinement min/median/max ms | Whole preprocessing median ms |
| --- | --- | --- |
| Fixed-hash production | 1.722 / 1.723 / 1.732 | 11.888 |
| Restored cold-path prefetch | 1.517 / 1.524 / 1.525 | 11.567 |
| Direct permanent space storage | 1.498 / 1.507 / 1.517 | 11.583 |
| Both changes | 1.534 / 1.540 / 1.559 | 11.590 |

`directspace` is closest, but its median remains above 1.5 ms on both SQLite
inputs (3.53.2: 1.494 / 1.520 / 1.525 ms), before its remaining setup charge.
Its setup process medians are 0.061555–0.062267 ms, versus fixed
0.070962–0.075892 ms; `directcold` reports 0.061656–0.062787 ms. None qualifies.
Current main.c whole medians are fixed 42.016 ms, coldpref 41.801 ms,
directspace 41.816 ms and directcold 41.872 ms; directspace's maximum 42.191 ms
overlaps the fixed range. The libc regression is repaired by `directcold`:
27.657–27.689 ms versus fixed 28.144–28.193 ms. Its standalone SQLite kernel
also improves: 1.318–1.326 versus 1.349–1.364 ms sustained, and 1.337–1.372
versus 1.404–1.413 ms after scalar conditioning. This does not compensate for
its larger charged refinement phase. No performance candidate is selected.

`directcold` already passes [local full refinement](directcold-refinement-local.txt),
[ASan/UBSan guarded preparation](directcold-preparation-sanitizers-local.txt),
[newer-source shared-file checks](directcold-newer-source-shared-local.txt),
and [6,000 newer-source C17/C23 comparisons](directcold-newer-source-preprocessor-local.txt).
Local performance is not 9700X acceptance.

## Unselected experiment preserved by this handoff

`plainkeys` builds on `directcold`. It uses the existing source-region bitmap
to select a pass-B specialization without repeated escape checks for a block
whose identifier spans cannot contain escapes. Full-file bitmap scans mark
backslashes (and '?' for trigraphs); the precise lexer handoff marks checked
splices plus every scalar-recovery region, which includes UCNs and joining
splices. The check includes the last identifier's complete physical extent.
Long identifiers retain the scalar normalization path. The scalar oracle is
unchanged.

The three `plainkeys-c_*.patch` files and [hashes](plainkeys-overlay-sha256.txt)
preserve the exact experiment. It has **no 9700X result**. Local evidence:

- [Full refinement](plainkeys-refinement-local.txt): 20,000 random inputs,
  the spelling/page-edge suites and 9,171 repository/SQLite/libc files pass.
- [Guarded sanitizer checks](plainkeys-preparation-sanitizers-local.txt):
  134,784 preparations, 1,200 explicit boundaries, both SQLite versions,
  compiler source and allocation failures pass.
- [Mixed handoff test](plainkeys-random-test.c),
  [ASan/UBSan result](plainkeys-random-sanitizers-local.txt): 2,000 larger
  mixtures of ordinary, Unicode, named-UCN and spliced identifiers pass.
- [Local timing](plainkeys-preprocessor-local.txt) is diagnostic only:
  directcold 2.441–2.529 ms versus plainkeys 2.378–2.474 ms, with overlapping
  ranges and uncontrolled local background activity. It does not select it.
- [Local ordinary-path assembly](plainkeys-ordinary-local.asm) confirms the
  repeated byte-minimum/escape-test sequence is absent from that specialization.

[handoff-prepare-test.c](handoff-prepare-test.c) is the prepared permanent
regression helper, still outside production. It combines the guarded cases,
2,000 mixed cases, five initial allocation failures and a partial-progress
allocation failure. The latter exhausts permanent storage after the first
completed block and verifies that the file is not published. Its
[local result](handoff-prepare-test-local.txt) passes with `plainkeys`.
[handoff-prepare-main.c](handoff-prepare-main.c) is the standalone wrapper.
To integrate later, include the helper from `c_preprocessor_test.c` and call
`pp_test_refine_handoff()` from the no-file fixture path. Review style and
current #133 fixture ownership before editing that shared file.

## Inputs and reproduction boundaries

- Frozen experiment source: `b1cb4be1bf7d3f41735b10a809cf221f5dde8cfa`.
  Target `/home/david/buster-fast-bench/codex-116/fifth/`;
  overlays in adjacent `fifth-overlays/`; local `work/frozen-b1cb/src`.
- SQLite 3.53.4 SHA-256:
  `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189`.
  SQLite 3.53.2:
  `0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d`.
- Target inputs: `/home/david/buster-fast-bench/codex-116/inputs/`
  `sqlite3.c` and `sqlite-new.c`. CI copies and corpus paths are recorded
  directly in `ci-splice-screen.sh`. All 169 system headers are pinned by
  `sqlite-system-headers.sha256`.
- Zig 0.14.1 libc: 8,983 C/H files, 93,836,361 bytes, target
  `/home/david/azp-bench/c-tokenizer/corpus/libc`.
- Target: `david@benchpress`, Ryzen 7 9700X, Clang 23.1.1, CPU 2 performance,
  SMT sibling 10 offline. Every manual build/test/profile/benchmark must use
  `EXCLUSIVE_OWNER=... ~/buster-fast-bench/exclusive.sh` per run, with CI
  priority. Native `ab --lock ...` takes the lock per child; do not nest locks.
- Normal candidate flags include `-O3 -g -march=native -fwrapv
  -funsigned-char -fno-strict-aliasing -Wall -Werror` and
  `-DBUSTER_REFINE_SOURCE_CHUNK=131072`. Overlay includes precede `-Isrc`.
  Do not mix an old preprocessor source with a newer benchmark's changed fields.
- Local Ryzen 9 measurements are diagnostic only. Use absolute nice 19,
  idle I/O priority and one heavy local operation at a time.

## Remaining work for the next owner

1. Claim #116 and the bounded #117 preparation scope, refreshing current owners
   and main. Read both completed CI records above, including the exact
   synthetic merge/source hashes and all measurements.
2. Keep the 1.5 ms charged target. Repair any libc/whole-workload regression.
   Qualify `plainkeys` only if further work is needed; its current evidence is
   local, with overlapping timing ranges.
3. Select a candidate only after repeated frozen-input target comparisons.
   Apply it to current production sources, integrate the regression helper,
   and clean prototype comments: duplicate SPDX in c_refine, misplaced lexer
   range comment, obsolete prefetch prose and arena-allocation wording.
4. Repeat final-production validation and complete workloads, including actual
   first-touch/setup/adjacent costs, both SQLite versions, compiler input,
   libc, kernel power states, scalar/guarded/shared-file/Clang checks and
   emitted assembly. Candidate-overlay CI is not final-source acceptance.
5. Remove the temporary branch-only qualification step before merge; its
   baseline source checksum intentionally rejects an already-modified
   production c_refine. Update the design/commands/evidence documentation.
6. Review the exact final head, pass required CI, integrate through protection,
   verify main containment and main CI, then close #116 and release claims.
   No merge, main-containment or closure result exists for this PR yet.
