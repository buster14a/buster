# Ordinary snapshot native phase ownership

This is a staged prerequisite for #3211. The default ordinary comparison is
still `legacy-rebuild`; this change does not activate snapshot preparation or
qualify a performance result. Integration follows the frozen closure stage,
the separately disabled qualification route, then this bridge.

`compiler_compare.py --closure-policy snapshot-v1` requires an explicit
`--closure-driver`: a canonical immutable native executable built by the
trusted checkout before candidate work. It must live in that checkout's
canonical POSIX bootstrap cache. The bridge records its SHA256, trusted Git
revision/tree, and the actual immutable completion manifest. The native
producer verifies that manifest's executable and complete source dependency
hashes and the initially pinned marker SHA before every phase. Candidate build drivers cannot select this helper.
Trusted bootstrap overhead belongs to the whole host observation.

The native interface is:

```text
compiler_closure owned-phase RECEIPT CWD TIMEOUT_SECONDS DRIVER_SHA256 BOOTSTRAP_SHA256 -- COMMAND [ARGUMENTS...]
```

The trusted caller supplies argv; the native owner checks bounded inputs,
consumes an exclusive receipt claim before spawning, owns the existing OS
manager lease, and uses the Linux subreaper before returning. SIGINT/SIGTERM
set signal-safe flags; signalling and exact-PID waiting remain native.
Adopted private process groups and sessions must be killed and reaped before
the phase ends. A nominal success that required orphan recovery is invalid.

All snapshot build, checkout, snapshot/restore/verify, lab, corpus, optional
inline and scaling runs use this owner. Measurement-time compiler/resource
and Git probes also use it. Historical preflight identity/request/toolchain
reads and the default legacy process lane retain their current behavior.
Controlled snapshot Git checkout disables automatic maintenance and hooks
per command. No global host configuration changes.

The minimal Python bridge launches the exact immutable executable directly.
On cancellation it signals only its still-owned positive Popen PID and waits
for native cleanup. It does not use a released process-group identifier.
Every prelaunch, checkpoint, native, proof-binding or log-publication failure latches the attempt stopped before propagating. Repairing the driver or filesystem cannot admit another phase. Expected read-only probe failures must be normal POSIX exits; a signal-crashed probe aborts the attempt. Every failure stops all later physical stages and destructive operations.
Missing receipts, manager ownership loss, or cleanup uncertainty leave the
attempt work intact with `cleanup-uncertain`; read-only diagnostics remain
available. Recovery cannot turn a failed phase into a successful measurement.

Each ordinal retains native JSON, length-prefixed argv, stdout/stderr, and
the immutable bootstrap completion manifest under `owned-phases/`.
The bounded data consumer binds command, canonical cwd, timeout, native
driver, trusted dependency inventory, ordinal path, log hashes, manager
release, and adopted-child proof. It requires the complete ordinary
build/lab/corpus/extension phase population. There are at most256 phases,
128 argv members,256KiB of encoded command, and8MiB per captured stream.
Exceeding a bound is a failure, never silent truncation.

Native duration covers entry through cleanup and log publication, before
the terminal receipt. Its own publication time is explicitly unavailable.
The bridge records the observed wrapper wall including receipt publication,
process exit, raw binding reads and bridge log publication. Neither this partial timing nor a sum of
phase spans is labeled complete job net cost. Whole admitted before/after
observations must include trusted bootstrap, raw export and final publication;
their authenticated job occupancy and any still-unobserved terminal/API tail
remain separate. Original10m lab counts/statistics and full corpus settings
are unchanged.

Before activation, the trusted publisher must bind expected preparation
policy from committed trusted route/configuration or authenticated API
context. It must call `validate_closure(..., expected_policy="snapshot-v1", require_owned_phases=True)`
with all retained owned-phase bundles; optional expected trusted revision and
driver SHA bindings are supported. Receipt-claimed policy is not authority.
Stripping closure/policy/phase fields cannot downgrade such a route to the
historical legacy interpretation. Existing legacy receipts remain readable.

Hosted tests execute the real ordinary bridge and native OS manager on
nominal, timeout, SIGTERM, SIGINT and escaped-grandchild cases, replay the
actual native receipts, and verify no subsequent child or late marker.
They are functional diagnostics. Performance qualification still needs the
approved9700X before/after and all predeclared A/A/corpus controls; failed
controls preserve the legacy default.

The ordinary corpus phase has one explicit `corpus-report-only-v1` exit
policy. Its immutable native command, full frozen corpus arguments and matched
root are checked before launch. A clean normal exit 1 remains a native failed
record and is accepted as complete report-only data only after the exported
summary and metadata pass the complete cell, count, profile and binary identity
rules and contain positive counted confirmed regressions. The new exit policy
also requires finite wall/RSS median pairs, every original per-round numeric
field and sample count, mandatory regression booleans consistent with the
reported p-values and original alpha, original workload input/profile facts,
and the unchanged producer decision predicates. Empty medians or test slots
without normal numeric data cannot admit a later child. Both exported raw
hashes are bound to the phase row and replayed by the data reader. Missing or
invalid reports, other statuses, a captured probe or a different command stop
the snapshot context before any later child. Legacy producers make the same
complete-data distinction; historical failed receipts stay failed.

Physical native owners acquire the shared durable ACTIVE cleanup lease before
children. Exact manager cleanup and adopted-child quiescence are required by
the shared supervisor before it releases that lease. Hard kills or unknown
ownership leave ACTIVE or UNKNOWN evidence for the shared admission guard;
ordinary bridge work retention alone does not authorize another physical job.
Foreign hosted diagnostics do not inspect or mutate the physical guard paths.
The shared workflow gate and physical cleanup proof remain prerequisites to
activation.

Hosted CI retains the full ordinary diagnostic receipt, all native phase
proofs, closure manifests and exported lab/corpus data for 90 days. The fixture
also exercises a complete corpus regression with real exit 1 and five
incomplete exit-1 cases, each proving no later child and retained failed data.
These are functional diagnostics and never performance qualification.

The native five-series qualifier uses the same exact corpus-only distinction.
Its typed corpus adapter constructs the frozen native command itself, checks
normal raw status 0 or 256, rechecks the frozen harness digest/size/mode and
absence of the output directory immediately before launch, validates both bounded JSON documents with the
existing duplicate-key-rejecting native parser, and binds both raw hashes to
the native cleanup record before any binary-after probe or next series.
Generic build, lab and probe commands retain zero-only completion. Raw 256
remains a native failed state; a complete phase ledger means complete report
data, not a successful zero exit or statistical qualification. The data
adapter replays the exact raw status, policy and report bytes independently.

Historical disabled staging heads #3221 and #3217 contain the original
zero-only corpus behavior. The coherent disabled integration must include
this follow-up before native qualification or utility acquisition runs.
The full five-series regression-exit diagnostic uses a fixed hosted-only
synthetic corpus charge; its true phase wall must contain every declared raw
sample wall. It refuses the protected host and physical request environment.
No physical margin, cell count, profile or acceptance criterion is relaxed.

## Explicit Utility ownership and main preflight

The disabled Utility route passes `--utility-owned-phases --closure-driver PATH` in both main-mode legs. It preserves the legacy BASE → HEAD → BASE recipe and original build-driver corpus dispatch, while the snapshot leg preserves restore and the frozen native corpus harness. Every child, including the context's trusted source observations, toolchain versions and main source-identity probes, finishes its native ownership proof before a later child is admitted. A clean terminal generic exit 125 remains a failed phase even when cleanup recovery proves quiet. Cancellation, timeout, missing or uncertain proof stop all later children and retain the attempt; the durable physical guard also blocks future admission when cleanup is unknown.

Utility receipts use `buster-compiler-utility-phases-v1`, require `owned_preflight: true`, and retain all ordinal records, commands, logs and bootstrap proofs for both legs. The trusted publisher explicitly requires that schema and ownership in both legs; an absent legacy closure cannot bypass this requirement. The exact legacy recipe has 11 core commands and the snapshot recipe has 12. Only the ordered original read-only Git/version metadata probes may precede those commands. Missing optional tcc, perf or taskset executables are recorded as NA without a child attempt. Missing configured clang, CMake, Ninja or Git tools stop preflight; required compiler and bootstrap identities remain mandatory. Owned preflight defaults unspecified capture working directories to the canonical trusted checkout, so invoking the trusted script from another directory preserves the same recipe. Both leg clocks include this supervision and its proof publication; no diagnostic timing qualifies host savings.

Normal main snapshot execution also starts owned preflight before metadata and identity children, retaining its existing snapshot population schema. Its future trusted activation caller must pass `require_owned_preflight=True` alongside the expected snapshot policy and ownership requirement, preventing removal of those proofs from downgrading the new contract. Historical receipt interpretation defaults to false. Historical legacy execution and all pull preflight ordering remain unchanged. The default preparation policy remains legacy until the separately admitted comparability and whole-job utility controls pass.

## Supported automatic-main route (dormant)

The main-only `--main-owned-phases` option requires a canonical, identity-validated
`--closure-driver` and is exclusive with `--utility-owned-phases`. The native
`compiler_closure driver-path` command reports only its current executable after
checking that executable's exact bootstrap marker, artifact and dependencies.
A trusted caller bootstraps this driver before candidate work and observes its
identity; a candidate path or cache glob cannot select it.

The new `buster-compiler-main-owned-phases-v1` schema owns every preflight child
and each of the eleven legacy or twelve snapshot core children. Its reader
requires the trusted caller to supply `expected_policy`, `expected_profile`,
`require_owned_phases=True` and `require_owned_preflight=True`. Missing or stripped
phase/profile fields cannot select a historical contract. Both policies retain
the full twelve-cell, two-round throughput corpus, and a clean, fully validated
counted regression remains complete report-only evidence. Exit 125, cancellation,
timeout and unproven cleanup stop before any later child.

`--main-profile compiler-compare-v1` retains the original ten-minute lab recipe.
`--main-profile compiler-main-40pairs-v1` selects exactly forty pairs with
`--target-minutes 10 --warmups 1 --pairs 40 --seed 20261003 --min-effect 0.5`
and CPU 2; its lab deadline is 300 seconds. The pinned lab retains fresh copies,
ABBA order, 95% confidence and 2000 bootstrap resamples. The fixed named profile
adds no arbitrary count, target, inference or workload selectors. The raw reader
must independently replay the exact config, plan, forty complete paired runs and
unchanged statistics. Historical PROFILE, pull and explicit Utility replay remain
stable. New Utility physical legs use the supported MAIN-owned long route, so
both treatments measure the same supervision contract and include its cost.

Eligibility, feature activation and rollback are authenticated trusted-policy
choices that bind policy revision P to a reviewed frozen measurement revision H.
These options alone grant no physical authority. The rollout remains disabled
until the predeclared preparation, comparability, sampling and complete whole-job
utility evidence is reviewed. Rollback retains the supported owned legacy long
route at H. Historical unwrapped job costs cannot establish its utility.
