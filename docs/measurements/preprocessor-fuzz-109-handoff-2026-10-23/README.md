# Issue 109 handoff, October 23, 2026

The final qualification is incomplete. This checkpoint preserves a partial
pragma-token oracle and the exact cases that still prevent acceptance.
It is not ready for integration.

Issue: <https://github.com/buster14a/buster/issues/109>.
Branch: `codex/109-final-qualification`, based on main
`a0c6176b11edacf5343ea3caae91e7c32357e224`.
Session: `01a16898-de27-7a20-bfb0-5441418a0c2f` (Solve issue 109).
The issue handoff comment records the published checkpoint SHA and releases
this session's bounded claim at David's request.

## Qualification result

The baseline public command on the Ryzen 9 7940HS, Clang 23.1.3, was:

```sh
./build/compiler test-preprocessor --clang --fuzz 200000 --first 0 --seed 201 --jobs 8
```

The source and binary were frozen at the base revision above. The binary
was in its own full tree so its bundled headers remained available. The
process ran at niceness 19 and idle I/O priority, with an on-disk `TMPDIR`.

| Mode | Generated | Verified against Clang | Unverified | Kernel differences | Observed differential mismatches | Missing results |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C17 | 200,000 | 199,997 | 3 | 0 | 0 | 0 |
| C23 | 200,000 | 199,997 | 3 | 0 | 0 | 0 |

The command exited 1 and the campaign stopped. This failed slice contributes
no successful final qualification. No ten-million-case campaign on either
host is complete.

`baseline-seed-201.out.txt` and `baseline-campaign.log` retain the original
receipt. A diagnostic entry point calling the same `pp_fuzz` implementation
identified seed 201's cases **20983, 30319 and 89431**. These diagnostic
replays are not additional qualification counts.

## Partial implementation

- `tools/preprocessor_pragma_oracle.cpp` uses the installed Clang C++ frontend
  API to observe expanded `#pragma omp` operands. It reads the original source
  without rewriting it and dumps actual Clang token kinds and spellings.
- Its small build script is optional for local builds. The ordinary compiler
  build still produces its existing executable. The proposed workflow step
  builds the external oracle afterward and sets `BUSTER_PPF_PRAGMA_ORACLE`.
- The C harness recognizes separately tagged pragma records, checks the exact
  Clang release version, and compares complete opaque-token spellings. With
  no independent evidence, it retains the unverified accounting and now prints
  the seed/case ID. Ordinary Clang token-dump verification remains in use.
- Added controls exercise trailing literal spaces, nested invocations,
  `__COUNTER__`, `_Pragma`, oracle failures, tagged records and release-version
  mismatches. CI wiring and the latest added controls remain unvalidated at
  this checkpoint.

The adapter's C++ is required by Clang's frontend API. No Clang implementation
or headers are vendored. The applicable expansion rule was read in LLVM's
`llvmorg-23.1.3` `clang/lib/Frontend/PrintPreprocessedOutput.cpp`; the adapter
and C comparison changes are original project work.

## Known blocker

The actual seed-201 cases involve ordinary/unknown pragma operands produced
through `_Pragma`, including stringization of malformed tokens. The prototype
currently observes only `omp` operands. **All three cases remain unverified
in both modes even with the adapter present.** Their per-case replay receipts
are under `cases/`; the `.c` files are the original generated C17 inputs,
not minimized or newly executable regression fixtures.

To reproduce through the public command after rebuilding this checkpoint:

```sh
bash tools/build-preprocessor-pragma-oracle.sh
export BUSTER_PPF_PRAGMA_ORACLE="$PWD/build/preprocessor-pragma-oracle"
./build/compiler test-preprocessor --clang --seed 201 --first 20983 --fuzz 1 --std c17
./build/compiler test-preprocessor --clang --seed 201 --first 30319 --fuzz 1 --std c17
./build/compiler test-preprocessor --clang --seed 201 --first 89431 --fuzz 1 --std c17
# Repeat with --std c23. --print-case ID --seed 201 --std MODE regenerates each input.
```

## Validation boundaries

These are observations of intermediate, uncommitted prototype builds based
on the frozen base, not validation of this final checkpoint SHA:

- The external adapter built locally against Clang/LLVM 23.1.3.
- The public preprocessor command passed a 10,000-case-per-mode prototype
  preflight with zero differences, missing results or unverified cases, and
  all 16 boundary/mutation controls passed (`prototype-preflight.out.txt`).
- The independent harness passed 1,484 checks in each mode
  (`harness-c17.out.txt`, `harness-c23.out.txt`).
- The normal Clang-free preprocessor command passed with 100 generated cases
  per mode. Later failure/forged-marker controls and small source edits were
  made after those builds. The current checkpoint needs a fresh build and
  complete relevant checks.
- The source diff and build-glue shell syntax checks passed at handoff.
- The approved-9700X build waited behind CI and was stopped while still
  waiting; its wrapper exited 143. It never produced a compiler executable.
  No target-host validation or hosted CI for this work is claimed.

`prototype-binaries.sha256` identifies retained local diagnostic executables;
it does not certify this source checkpoint. The executable files themselves
are not committed.

## Next owner

1. Claim the remaining issue scope and review this partial implementation,
   including the external API adapter and proposed CI dependency.
2. Extend independent pragma observation to the ordinary/unknown operands
   found above. Keep ambiguous cases unverified until actual token evidence
   exists. Minimize these inputs and make the cases permanent executable
   fixtures; preserve the shortened-literal mutation and failure controls.
3. Update from current main and coordinate any overlapping production edits.
   This checkpoint changes test/oracle code, not the production preprocessor.
4. Build and run the preprocessor fixtures, both harness modes and the call
   graph check. Obtain locked-9700X and hosted validation for the resulting
   exact revision.
5. Freeze a fresh source revision and run ten million fully verified cases
   per C17/C23 mode over several fresh seeds on each host. Retain per-slice
   receipts; reject any missing, unverified or differing case. Use the normal
   per-run shared lock with CI priority, about two-minute holds with pauses,
   and low-priority local runs with on-disk scratch storage.
6. Publish the final receipts and follow protected integration before treating
   issue 109 as complete. Previous failed/stopped campaign counts do not
   satisfy that acceptance.

The local campaign is stopped and has a STOP file. No compiler, fuzz worker,
remote build, lock reservation or CI launch remains owned by this session.
Scratch evidence is retained at
`/home/david/Documents/Codex/2026-10-23/so-3/work/qualification/` and the remote
staging tree at `~/buster-fast-bench/codex-109-01a16898/`.

## Generated-input provenance

The three C inputs were emitted by this repository's original
`preprocessor_fuzz_generate.c` at base `a0c6176`, using seed 201, the case IDs
in their names, and C17 mode. The replay command regenerates both C17 and C23
variants using the same unchanged generator. Clang is an external oracle;
no external implementation or fixture source is copied into these inputs.
The logs are original observations of those project-generated inputs.
