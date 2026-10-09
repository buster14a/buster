# Source-size report and change ratchet (#1581)

`build.c` owns this report through `tools/source_size.c`. It measures the
tracked files of a Git revision, classifies them, and ratchets hand-maintained
production and build/orchestration code so growth is visible in every pull
request and a large increase needs an explicit, reviewable acknowledgement.
Tests, generated data, documentation and dormant preservation material are
reported separately and are never ratcheted.

```sh
./build.sh source_size                                  # HEAD: totals and drift since the baseline
./build.sh source_size --base "$(git merge-base origin/main HEAD)"   # this branch's change, with the ratchet
./build.sh source_size --rev <revision> --base <revision>
./build.sh source_size --write-baseline                 # acknowledge: refresh the committed baseline from HEAD
./build.sh source_size --self-test
```

Every input is a committed revision: the file list and blob sizes come from
`git ls-tree -r -l`, and the baseline is read from the measured revision, not
the working tree. Commit before measuring. The result is therefore a function
of the revisions alone and is identical on every host and driver compiler.
Sizes are blob bytes as stored in Git, independent of checkout line-ending
conversion; gitlinks carry no bytes. `--rev` defaults to `HEAD`.

## Output

One invocation prints both forms. The human part is a category table (files,
bytes, change against `--base`, drift since the baseline), the ten largest
ratcheted files, the ratcheted files that changed against `--base` (the forty
largest changes, renames as a removal plus an addition), and one verdict line
per ratcheted category. The machine-readable part follows as space-separated
`key=value` records whose values never contain spaces:

```text
SOURCE_SIZE_V1 revision=<sha> category=<name> ratcheted=<0|1> files=<n> bytes=<n> base_files=<n|-> base_bytes=<n|-> baseline_files=<n> baseline_bytes=<n>
SOURCE_SIZE_RATCHET_V1 category=<name> growth=<+n|-n|0> limit=<n> verdict=<within-limit|acknowledged|unacknowledged>
SOURCE_SIZE_RESULT_V1 revision=<sha> base=<sha|-> baseline_commit=<sha> status=<pass|fail>
```

`SOURCE_SIZE_RATCHET_V1` appears only with `--base`. The command exits nonzero
when a revision, listing or the head baseline is missing or malformed, or when
a ratcheted category fails.

## Categories

`source_size_rules` classifies a path by its first matching rule; the
self-test pins representative paths from every rule.

| Category | Ratchet | Rule, in match order |
| --- | --- | --- |
| `dormant` | no | Any `*.bbb`, and `DORMANT_CUSTOM_COMPILER.md` |
| `docs` | no | `docs/**` (audits, evidence, ledgers, this baseline), `LICENSES/**`, any other `*.md` |
| `generated` | no | A path with a `generated` directory component, or a file name containing `.generated.` |
| `tests` | no | A `tests` or `fixtures` directory component; a file name starting `test_`; a stem (name before the first `.`) that is `test`, `tests` or `fixture` or ends in `_test`, `_tests` or `_fixture`; anything under `benchmarks/9700x/` (9700X workload payloads, their input data and the compiler-comparison request, #2770) |
| `build` | yes | A `tools` directory component, including generator scripts nested under `src/` |
| `production` | yes | Everything else under `src/` |
| `build` | yes | Everything else: `build.c`, the bootstrap wrappers, CMake, `.github/`, `tools/`, `android/` and `ios/` scripts, root configuration |

The fallback is deliberately a ratcheted category: code in a new location is
measured until a rule deliberately exempts it, so moving code cannot hide it.
Test harness scripts named as tests, such as `android/test_ci.sh`, count as
tests.

## Ratchet

For each ratcheted category a change may grow the total by at most
`SOURCE_SIZE_CHANGE_LIMIT_BYTES` (32 KiB) relative to its base. Beyond that the
same change must also change that category's row in
`docs/source-size-baseline.txt`, normally by running
`./build.sh source_size --write-baseline` after committing the growth, and its
description should explain the growth. That diff is the acknowledgement a
reviewer sees. The check compares the rows at the base and the head, not exact
totals, so the acknowledgement survives a rebase or a merge-queue merge. A base
without a baseline, or with a malformed one that the change repairs, predates
the ratchet and acknowledges.

PR #3154 acknowledges the per-concrete-union state ledger in
`src/buster/lib/compiler/frontend/c/c_gen.c`, which adds 44,584 production
bytes relative to the `main` tip used for this acknowledgment. The generated
baseline records 18,640,460 production bytes, including 99,945 bytes of
accumulated drift since the prior baseline. The ledger retains active union
arms by concrete object across ranged writes and aggregate clears, with
static-image and runtime regressions for same-arm retention, arm switches,
range coverage and deeply promoted anonymous members.

Categories are independent. Deleting tests, fixtures, generated tables or
documentation never earns room for production or build code, and only the
ratcheted totals are limited, so moving code between files or splitting a file
is neither a reduction nor an increase. Small changes pass without touching the
baseline; their cumulative drift is the `since baseline` column. Refreshing
the baseline is also how anyone records accumulated drift or a reduction.

Calibration: over the 69 first-parent merges to main from `f7bd9f2` through
`1edfe98` (2026-09-23 to 2026-09-27), production grew 180,372 bytes and build
127,697 bytes. One change in each category exceeded 32 KiB: #1210 (a new
wasm32-wasip1 target, production +47,164) and #1134 (the systemd broker, build
+48,498). The next largest were production +20,100 and build +14,379. At that
rate the limit asks 2 of 69 changes for an acknowledgement; at the roughly 50
bytes per line of `build.c`, 32 KiB is several hundred lines.

## Baseline

`docs/source-size-baseline.txt` is generated; do not edit it by hand. It
records the schema, the full commit it measured, and one
`<category> <files> <bytes>` row per category. The initial baseline records
the audited commit `ade6ac4b6ecb21f30b61b656439bac476c145e2f` named in #1581:

| Category | Files | Bytes |
| --- | ---: | ---: |
| production | 179 | 15,796,488 |
| build | 182 | 5,590,004 |
| tests | 708 | 14,290,839 |
| generated | 47 | 51,745,042 |
| docs | 1,773 | 49,724,450 |
| dormant | 65 | 88,893 |

At that commit `build.c` was 1,998,166 bytes, `CMakeLists.txt` 105,940 and
`.github/workflows/ci.yml` 76,652. The largest ratcheted file was
`src/buster/lib/compiler/frontend/c/c_gen.c` at 2,512,049 bytes.

## CI

The required `Canonical TCC bootstrap` job runs `source_size --self-test`
through the TCC-built driver, fetches `GITHUB_SHA` with its first parent
(`--depth=2`), and reports `--rev "$GITHUB_SHA" --base "$GITHUB_SHA^1"`. For a
pull request `GITHUB_SHA` is GitHub's PR merge revision, whose first parent is
the base branch tip, so the deltas are exactly what the merge adds; for a
merge group it is the group commit over its base, and for a push to `main` the
new tip over the previous one. The report is printed in the log and in the job
summary, and an unacknowledged increase fails the job. The repository is
public, so the fetch needs no credentials. The workflow checks out `GITHUB_SHA`
for the TCC driver and every bootstrap/component check; it is the PR merge
revision, exact merge-group commit or pushed main commit. Before running, the
job asserts and records the checkout SHA, tree SHA and `build.c` blob SHA so
the driver source is identifiable alongside the source-size subject.
