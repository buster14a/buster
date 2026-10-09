# Native preparation qualification v1

This is the separate preparation experiment for #3211, admitted through #3217's existing trusted native boundary. It does not use the sampling freeze schema or shorten a sampling profile. The checked-in allowlist is disabled and its unobserved hashes remain `-`. No physical outcome has been collected by this policy.

The pure policy is `tools/compiler_preparation_qualification_admission.c`, included after the existing native sampling admission helpers. It parses a distinct reviewed plan and reuses their strict TSV, decimal, timestamp, identity and first-claim history boundary. The trusted API adapter and native controller establish the supplied facts; successful policy parsing alone does not authenticate transport or qualify the experiment.

## Frozen plan and selector

The exact-byte plan is [compiler-preparation-plan-v1.tsv](compiler-preparation-plan-v1.tsv), schema `buster-compiler-preparation-plan-v1`. The allowlist [compiler-preparation-admission-v1.tsv](compiler-preparation-admission-v1.tsv) uses `buster-compiler-preparation-admission-v1` and only `state=qualify` can admit. Its immutable plan commit, complete plan SHA-256, protocol SHA-256 and historical lower bound must be reviewed and committed before a fresh owner request.

| Role | Immutable source | Tree |
| --- | --- | --- |
| G baseline | 942ee124bf41601a7ae81d8ae82dc5151e2ee2e0 | 075022a8dfc83b7cdd200f6bf4eca88d58ddb7bf |
| AB1 candidate | 7d962dff641aa3a340920f3720a5e1fc1d7e259a | 0ddecece17d892dd6793b30e94e004b20bce639f |

These are source pins, separate from any PR combined test tree, compiled binary and fresh request head. The native controller must verify both exact Git revisions and trees. The strict 30-field plan also pins measurement-harness `trusted_revision`, complete protocol bytes, lab script, independently observed Python executable path and SHA-256, and trusted native-driver binary SHA-256. Those unavailable tool/harness identities stay unfilled. Artifact manifests or candidate-provided labels cannot attest them. The current executor-policy revision comes from authenticated facts and remains distinct from the plan's measurement-harness revision.

The authoritative versioned protocol file to hash is this file, `docs/compiler-preparation-qualification-v1.md`, at the plan's trusted measurement-harness revision. The lab has its fixed trusted-root-relative path; Python must use the reviewed absolute `python_path`, never an unbound PATH lookup. The controller verifies realpath equals the approved path, nofollow regular/executable identity and the independently pinned SHA-256 before invoking it. It verifies its exact native-driver executable against the approved digest.

After that reviewed freeze, the sole supported fresh selector is:

```text
profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: <40-lowercase-hex-plan-commit>
```

This document does not add a selector to the request marker. Existing owner/repository checks, attempt-one rule, one or two parents and exact fresh marker on every parent remain mandatory. The authenticated facts use the existing 23-field `buster-main-sampling-github-facts-v1` transport. Unknown, duplicate, missing or malformed TSV fields fail.

Historical API attempts retain the canonical 16-column header: phase, packet, request_run_id, request_run_attempt, executor_run_id, executor_run_attempt, state, physical_wall_us, campaign, freeze_revision, actor_login, actor_id, triggering_login, triggering_id, pull_author_login, pull_author_id. Preparation attempts use phase `preparation` or `qualify`, packet zero, exact plan SHA-256 campaign and immutable plan revision. Before any clone, fetch or preparation, the authenticated API adapter must enumerate every prior profile/plan attempt, including failed, cancelled, hostless, incomplete and rerun attempts, and the native controller must acquire its exclusive claim. Only the exact header with no prior rows admits this first packet. A prior attempt cannot be suppressed or retried by filtering its outcome. A changed plan requires a separately reviewed pre-outcome experiment and retention of the old disposition.

## Native execution and retained evidence

ROOT is exactly `/tmp/buster-3211-closure-source`; OUT is exactly `/tmp/buster-3211-closure-output`. Both must be fresh canonical paths, disjoint from each other and outside the actual native-observed workspace and cleanup roots in both containment directions. The pure policy checks lexical paths and overlap. The native controller must compare wrapper arguments to its actual environment, prove canonical filesystem identity and freshness, and reject symlinks, existing campaign roots or races before claiming/fetching. Caller-supplied workflow paths do not establish that authority. These campaign-owned paths and partial evidence are retained on failure; ordinary cleanup must not delete them.

The native bridge owned by #3217 is:

```text
compiler_profile_qualification --admit-preparation ALLOWLIST MARKER FACTS HISTORY PLAN CLEANUP_ROOT WORKSPACE_ROOT OUTPUT
compiler_profile_qualification --execute-preparation --trusted-root PATH --cleanup-root PATH --evidence PATH
```

The second command has the internal owned-worker route. REQUEST_DATA, PLAN_DATA, ALLOWLIST_DATA, FACTS_DATA and HISTORY_DATA are the five bounded base64 transport records; their contents are revalidated before execution. The preparation controller, owned by #3211, invokes the existing trusted native driver directly:

```text
compiler_closure qualify ROOT OUT G G_TREE AB1 AB1_TREE LAB PYTHON
```

The plan's `command=compiler_closure-qualify-v1` is a schema label for this argv, not an additional executable. No new Python orchestration, workflow-side preparation or candidate-root execution is introduced.

The whole physical job reserves 5,400 seconds (90 minutes), including authorization/claim, fetch, preparation and publication. The native worker deadline is 5,280 seconds with 120 seconds reserved for terminal evidence and cleanup/publication. Exhaustion, cancellation, escaped child, failed cleanup, missing phase or incomplete publication fail closed; they do not authorize a longer budget or rerun. The controller owns monotonic timing and bounded process cleanup.

Qualification retains five lab repetitions and five complete compiler-corpus repetitions, legacy-rebuild versus snapshot-v1 controls, all original long-profile input/flags/settings and immutable source/generated/configuration/tool/root/harness identities. Keep every A/B interval and control, raw receipts, hashes and whole-operation costs. The existing native cost adapter validates them; `preparation-cost.json` and its publication pointers must be retained in every evidence export.

## Predeclared acceptance

Activation requires complete independent dedicated-host evidence. The observed complete snapshot preparation cost must be lower than legacy cost, including restore, proof and receipt publication. Unmeasured or shared overhead remains explicit and cannot be silently credited as savings.

All three immutable/cross-build A/A primary wall-time 95% ratio intervals must lie wholly inside [0.995, 1.005], every required corpus cell must be present, and no A/A corpus regression may be confirmed. Preserve original populations and settings; data completeness or process exit alone does not qualify activation. Any failed criterion, unavailable identity, incomplete attempt or budget exhaustion retains legacy-rebuild. This preparation result supplies no sampling-arm truth label or [2.0%, 2.5%] calibration claim.

The native fixtures exercise plan/facts/config ambiguity, hashes, source/control mutations, exact selector and all-parent freshness, owner/repository/attempt identities, path overlap, every prior-attempt state, an otherwise-valid prior sampling attempt, malformed history and metadata limits. Hosted controls validate policy/functional behavior only; full integration registration and physical qualification are separate gates.

Buster's first-party license remains unspecified in [LICENSES/README.md](../LICENSES/README.md); no upstream code is imported.
