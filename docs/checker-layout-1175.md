# Collecting checker-layout acceptance for #1175

The compiler change is already merged in [#1225](https://github.com/buster14a/buster/pull/1225).
The remaining acceptance is measured taken branches and cycles over placements on the 9700X.
Run that fixed historical comparison once through the existing
[Tailscale access and shared host lock](https://github.com/buster14a/buster/blob/main/docs/commands/benchpress.md), then preserve and review its
results. It does not need a dedicated Actions workflow, scheduled run, PR check or merge gate.
Ordinary correctness, sanitizer, fuzz and native CI continue under their existing policy.

## Fixed inputs and qualification

Use the integrated comparison and the reviewed strict counter reader:

- Reference: `e36740b8f70cf5755cabff9b2a9a998b173c2878`.
- Candidate: `c79fd0076fc8ee3ba0a43d02f94dca417a7557ea` (reference is its first parent).
- Harness: buster-performance `961e2893fe13ac4c30547ff86f1e2e279e09b06b`, the merge of
  [buster-performance#248](https://github.com/buster14a/buster-performance/pull/248).

Before using the host, verify the pins, candidate ancestry on main and successful correctness
for both exact historical source SHAs. Their main-push runs are
[reference 41839388530](https://github.com/buster14a/buster/actions/runs/41839388530) and
[candidate 41841112215](https://github.com/buster14a/buster/actions/runs/41841112215).
Read every page of each latest attempt's jobs: all 48 expected hosted shards across both
architectures and sanitizer modes, all reported hosted test rows, the three named fuzz jobs
and the native correctness step must have succeeded. Missing, skipped or failed correctness
refuses measurement. Preserve the run and job JSON alongside the results.

The pinned reader's 48 synthetic C tests passed with GCC, Clang and Clang
AddressSanitizer/UndefinedBehaviorSanitizer in
[hosted qualification 42010203366](https://github.com/buster14a/buster/actions/runs/42010203366).
That qualifies the harness; it supplies no hardware performance result. Keep these reusable
regressions with the harness. Repeating this historical experiment on every future compiler
change would measure the same old pair, so it is not an ongoing regression check.

## Running once

Work in a fresh task directory on benchpress, with separate `source` and `harness` checkouts;
do not build in another session's tree. Check out the harness at the exact pin above and fetch
both source SHAs. Stage the official Clang 23.1.4/build tools using
`tools/ci/setup-benchpress.sh` and extract and verify the frozen corpus with
`tools/ci/fetch-corpus.sh` into the task's own `corpus` directory. Take the host lock for each
setup operation, and apply the tool paths and environment those scripts produce. No system
package, runner configuration or access-policy change is needed.

With those inputs qualified and staged, run from the pinned harness checkout:

```sh
campaign_root="$HOME/buster-fast-bench/1175-session"
EXCLUSIVE_OWNER=layout-1175 \
EXCLUSIVE_TITLE='One-off checker layout acceptance #1175' \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/1296 \
CAMPAIGN_DIRECTORY="$campaign_root/results" \
CAMPAIGN_CORPUS="$campaign_root/corpus" \
CAMPAIGN_HOST_ARCH=znver5 CAMPAIGN_CPU=2 CAMPAIGN_RUNS=11 \
CAMPAIGN_EVENTS=instructions:u,cycles:u,ex_ret_brn_tkn:u \
bash docs/measurements/layout-hints-1175-2026-11-03/scripts/campaign-9700x.sh \
    "$campaign_root/source" \
    main=e36740b8f70cf5755cabff9b2a9a998b173c2878 \
    head=c79fd0076fc8ee3ba0a43d02f94dca417a7557ea
```

Leave `CAMPAIGN_LOCK` unset. The standalone campaign takes `exclusive.sh` separately for each
build, object comparison, placement's counter runs and timing run, releasing it between them
so CI and other sessions can use the machine. Do not put the whole campaign under another
lock, use `CAMPAIGN_LOCK=env`, stop the runner or hold the host while inspecting results.
Use the `cores` queue for builds where available, as the campaign already does.

The campaign builds four checker placements per tree: skip/shift pairs 0/0, 688/256,
1376/512 and 2064/768. It checks SQLite syntax, SQLite object compilation and 139 musl
object compilations. At each placement it collects three repetitions of each user-mode
counter and runs `time-runs --runs 11 --warmups 1 --copies 2`.

## Evidence and the remaining decision

Check the actual within-page displacements of `check_input_resolve_range` and `check_graph_run`
against the checker skips, and of `main` against the shifts. Require 140 objects from each
of the eight executables and identical full object-hash manifests. Preserve full executable
hashes and symbol tables, not only the campaign's abbreviated hash receipt.

Require all 216 declared counter observations, 72 unfiltered perf captures and a successful
strict-reader replay. Review timing process statuses as well as the placement/copy results.
A failed or incomplete run cannot establish acceptance; complete collection alone is not a
positive performance verdict.

Publish raw logs, qualification receipts, source/harness/tool identities, placements, hashes
and timings in `buster-performance/docs/measurements/`. Keep only a concise measured summary
and evidence links in this repository's `docs/compile-time.md`. Review cycles and taken
branches across placements, record regressions or inconclusive ranges, and document the
remaining gap to the profile's layout before deciding whether #1175 is complete.
