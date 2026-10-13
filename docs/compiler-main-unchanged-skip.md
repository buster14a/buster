# Unchanged-input MAIN benchmark skip (#3923)

The routine automatic MAIN compiler comparison
([admission guide](../benchmarks/9700x/ADMISSION.md#main-compiler-comparison))
measures every commit that lands on `main`. When the commits since the last
actual measurement provably change nothing the comparison builds or measures,
the request is completed as **not measured** without allocating the 9700X.
This page is the versioned classification contract
(`buster-ci-main-unchanged-v1`).

## Scope

- Applies only to routine automatic MAIN comparisons started by
  `.github/workflows/9700x-compiler-request.yml`, and only on the non-owned
  `compiler-compare-v1` / `legacy-rebuild` route. A MAIN-owned route always
  measures.
- Explicit pull-request comparisons (`compiler-compare.request`), research and
  qualification packets, and issue-specific acceptance keep their execution
  contracts even for unchanged source.
- Sample counts, statistical thresholds, correctness checks, merge policy and
  the #2761 actual-execution requirement are unchanged. A skip discharges no
  [performance-validation](performance-validation-v1.json) row.
- No packaged binaries, cross-run compiler cache or binary promotion is
  involved; a request that must run uses the ordinary build-and-measure path.

## Decision

`authorize-compiler` (hosted, read-only) already chooses the baseline: the
nearest first-parent ancestor, within the 100-commit chain it reads, whose
`9700X compiler benchmark` check completed with conclusion `success`
(`authorize_compiler.choose_base`). It now also reports whether that baseline
is such an actual measurement (`base_measured`) and links that check
(`measured_reference`). Only then, and unless the repository variable
`BENCH_COMPILER_UNCHANGED_SKIP` is `false`, two bounded steps run:

1. Fetch the pushed commit, the baseline and the trusted policy revision (the
   workflow's own `main` revision) into a bare repository: blobless, 130
   commits deep, credentials only for that one command.
2. Compile this trusted revision's `build.c` and run
   `ci_no_code --event main_push --base BASE --head HEAD --tested HEAD --policy POLICY`
   with `GIT_NO_LAZY_FETCH=1`.

The classifier (`ci_no_code_main_push` in `tools/ci_no_code.c`) returns
`unchanged` only when all of these hold:

- the baseline is a strict ancestor on the pushed commit's first-parent chain
  within 128 commits;
- the pushed commit is an ancestor of (or equal to) the policy revision;
- the measured subject (baseline to pushed commit) and the measurement policy
  (pushed commit to policy revision) are each prose-only.

Prose-only uses the [no-code allowlist](ci-github-actions.md#no-code-classification-and-staged-admission-3107)
unchanged: a small list of exact ordinary prose paths, regular `100644` blobs
only, additions, deletions and both paths of a rename checked separately, and
any mode, type or gitlink change, unknown path, incomplete diff or Git failure
selecting measurement. Everything else is execution-affecting, including all
compiler and shared sources, headers, generator producers and data, embedded
resources, `build.c`, CMake, toolchain, flag and target policy, workflows,
benchmark harnesses, workloads, profiles, selection and validation data, and
machine-consumed files under `docs/`. The classifier and its allowlist are
outside the allowlist, so a change to either measures and cannot authorize its
own exemption.

Both steps continue on error with a two-minute limit each. Any failure,
timeout, malformed receipt or `measure` decision leaves the job output
`unchanged=false`, and the comparison runs exactly as before.

### Non-file and revision-derived inputs

The ordinary recipe (`./build.sh generate --cc clang --no-include-tests`, then
`./build.sh build --config Release -t ide`) embeds no commit or tree identity.
The Git-derived build data that exists (`BUSTER_INVESTIGATION_REVISION`,
`BUSTER_PGO_MANIFEST`, the self-host stage-object tree identity) is opt-in or
outside that recipe. Because every non-prose difference measures, two builds
across a skip differ only in the commit ID, which the recipe does not consume.

The 9700X host's toolchain, kernel and physical conditions are not repository
inputs and cannot be established from the hosted runner. A skip therefore
asserts repository-input equivalence only, never complete input equality, host
state or unchanged performance; the report says so.

## Merge bursts

The workflow keeps one pending MAIN run and never cancels a running one, and a
skip is never a measurement (its check concludes `neutral`). Because the range
always starts at the last actual measurement:

- A measured, B changes the compiler and is pending, failed, cancelled or
  displaced, C changes only prose: C's baseline is A, the range A..C contains
  B's change, and C is measured.
- After a measured A, prose-only commits C1, C2, ... are each classified
  against A and skipped; none becomes a new reference.
- A run classified while a measurement of its baseline is in progress cannot
  exist: the shared concurrency group starts it only afterwards.
- No measured ancestor within the bounded chain, an unreadable listing, or a
  baseline beyond the shallow fetch measures.

## Publication

When the decision is `unchanged`, the `compare` job is not allocated and
`publish-compiler` (`compiler_publish.unchanged_proof`) re-validates it as
data: the exact receipt keys, schema, event, baseline, head, policy and
reason; that `compare` was skipped; the non-owned route; and, from GitHub's
current check records, that the linked reference is still a valid
measurement of the baseline. A refusal is reported in an ordinary failed
check rather than skipped silently.

The completed check is `neutral` with the title **Not measured:
compiler/build and benchmark inputs unchanged.** It records the head, the
request run and attempt, the executor run and attempt, the classifier schema
and trusted revision, the reference commit with its check link and completion
time, the range, the reason, the changed prose path counts, and the observed
hosted classification time. Avoided physical-runner time is not estimated. No
samples, verdict or 0% change is copied, and any regression the reference
reported stays where it was published. The commit report carries the same
entry with no evidence artifact. `publish-compiler` runs after
`start-compiler`, and the check writer never reopens a completed check.

## Rollback

Set the repository variable `BENCH_COMPILER_UNCHANGED_SKIP` to `false`: the
classification steps no longer run and every automatic MAIN request is
measured as before. Reverting the workflow change has the same effect.

## Tests

- `./build.sh ci_no_code --self-test` and the `No-code classifier controls`
  workflow, whose MAIN step covers A to B to C, several skipped commits,
  measurement-policy drift, a foreign policy, classifier changes, unknown
  Markdown, mode changes, renames, equal, non-ancestor, second-parent-only and
  missing baselines, the depth bound, and blobless, treeless and shallow
  candidates.
- `tools/bench_direct/authorize_compiler_test.py`: the reference exists only
  for an actual measurement; pending, failed, cancelled and neutral baselines
  fall back.
- `tools/bench_direct/compiler_unchanged_test.py`: publication of the neutral
  result, tampered or incomplete decisions, owned or executed attempts,
  references that changed while planning, and terminal-check idempotence.
- `tools/bench_direct/workflow_policy_test.py` pins the exact steps, outputs
  and conditions.
