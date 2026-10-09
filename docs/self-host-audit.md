# Self-host fixed-point audit

## Main-only rollout (#3045)

Move this heavy audit from admission to post-merge detection in two stages.
First land the admission/recovery compatibility change while all existing
triggers and the live required check remain in place. The trusted collector
accepts exactly the current eight-check ruleset or the reviewed seven-check
ruleset without `Linux x86-64 bootstrap evidence`; it collects the audit only
when the live ruleset requires it. All other checks, source apps, queue limits,
freshness, review and bypass policy remain enforced. A policy change during
the collector's two reads invalidates that attempt.

After that implementation is trusted on main, an administrator removes only
`Linux x86-64 bootstrap evidence` from ruleset 22537199, retains a before/after
read-back and audits the effective main rules. Then land the main-only workflow
and matching inventory/documentation change through normal admission. This
order lets both PRs pass without a missing-check deadlock, manual success or
bypass. Keep the expensive workflow intact until the compatibility PR lands.
For rollback, restore the pre-merge triggers before requiring the audit again.

The live settings edit is a separate administrative operation; repository code
and passing offline fixtures do not prove that it happened. Keep #3045 open
until both stages, the settings read-back and live acceptance traces are complete.

The native build driver owns this validation. On a configured Linux x86-64 tree:

```sh
./build.sh self_host_audit_self_test
./build.sh test_self_host_audit --config Release
```

Run `./build.sh generate` once before the second command on a fresh checkout.
Do not regenerate the tree or change compiler sources while an audit is running.
The `.github/workflows/self-host-audit.yml` job uses the documented hosted
Clang bootstrap exception and runs automatically only for `push` on
`refs/heads/main`. Checkout explicitly uses that event's immutable `github.sha`;
a subsequent main advance never changes the subject. PRs, merge groups,
feature pushes and tags do not start it. There is no manual dispatch: recover
by rerunning the original main-push run/failed jobs in Actions, retaining the
original SHA and attempt history. Each push gets its own concurrency group
with cancellation disabled, so newer pending pushes cannot displace older ones.
No batching or main CI reuse is applied to this audit. The full 120-minute
budget, ordinary bootstrap, alternate backends, repeated audit, compiler
regressions and independent differential probes remain intact. The existing
multi-platform/configuration matrix and ordinary `test_self_host` gate remain
in place before merge.

## What a pass means

Let C0 be the host-built compiler. Independently execute C0 twice to produce
C1a/C1b; C1a twice to produce C2a/C2b; and C2a twice to produce C3a/C3b.
Every child must compile successfully, produce complete evidence and pass its
behavioral probes before it can be used as a producer. Compare both repeats in
each generation, then C1a/C2a and C2a/C3a. This checks repeatability separately
from a three-generation fixed point; equality of only C1/C2 does not establish
repeat-run determinism.

Compare the following in pipeline order, stopping at the first mismatch:

1. Actual preprocessed token kinds and spellings, not just token counts.
2. Semantic canonical IR records and selected MIR records; selected MIR also
   runs the general verifier, including for selector-certified functions. A
   rejected function is never sent to allocation or dereferenced for tracing.
3. Compile stdout, stderr and exit status, then the complete generated binary.
4. Source metrics within every repeated generation and across C2/C3.

Physical lexing metrics are retained and validated for every compile, but are
not required to be equal between C1 and C2: C0 reads host resource headers,
whereas C1 reads Buster's built-in resources. Token evidence must nevertheless
match byte for byte across that boundary. The old comparator's token/byte
counts are now explicitly counts, never proof of identical input. Missing,
truncated, malformed, duplicate or unsupported-version required metrics fail
both the ordinary comparator and this audit.

C0 and every generated compiler also compile and execute the same behavioral
fixture in `fast` and `quality` modes. It covers pointer
joins, integer width/sign handling, floating/integer aggregate arguments and
returns, and register/stack argument boundaries. Variadic checks include named
integer/floating parameters, default integer/float promotions, ordered integer
and floating arguments exhausting both register files, and independent
`va_copy` cursors created before consumption and at an integer-register
boundary. Each copy remains usable after the original list is consumed and
ended. The fixture uses variadic builtins directly: it does not introduce host
resource headers into the exact per-probe token and source-metric comparisons.
Compare those mode-specific phase traces, binaries, outputs and statuses
against C0. An invalid-source fixture must be rejected without crashing,
timing out or producing an executable, with identical diagnostics and status
across generations. Captured sanitizer findings are failures even when the
child exits zero.

This is a finite, same-host, same-source, same-configuration empirical invariant,
not proof of compiler correctness, reproducibility across different SDKs/hosts,
or convergence from arbitrary compilers. The full compiler is bootstrapped in
its default allocator; both allocators are independently exercised by the
behavioral probes, not described as two full bootstrap fixed points.
The existing ordinary gate additionally exercises its supported alternate
backend builds. The stronger trace audit is currently Linux x86-64 only;
ordinary Linux/Windows x86-64 and macOS self-host coverage is unchanged.

## Independent behavioral evidence

C0 is still Buster, even when built by Clang. Agreement with C0 alone can retain
a semantic error shared by every generation. The dedicated CI job also passes
the same fixture to the existing native differential runner:

```sh
./build.sh test_differential --self-test
./build.sh test_differential --ide build/Release/ide --cc clang --source tests/self_host_bootstrap_probe.c --sanitize-oracle --out build/self-host-audit/probe-oracle
```

Once ordinary bootstrap succeeds, full compiler regressions and this independent
check still run when the enhanced audit fails. Each failure remains a job
failure; there is no `continue-on-error`. This separates semantic reference
evidence from the potentially failing generation comparison without bypassing
the latter. Cancellation does not start another test phase.

`--source` selects this one explicit input instead of the generated/default
corpus. The driver owns the discovered allocator/optimization/promotion matrix,
including `fast` and `quality`, and keeps code-generation
verification enabled. Independent Clang O0/O2 executions use address and
undefined-behavior sanitizers; their observations must agree before they can
serve as the reference for Buster. The ordinary audit separately requires the
fixture's successful exit and output, so an agreed nonzero exit in the general
differential runner cannot substitute for bootstrap success.

Choose a fresh output directory when repeating the command. Compiler identities
and hashes, configuration inventory, copied source, exact command vectors,
diagnostics and process statuses remain in the differential evidence under the
existing CI artifact. The comparison is behavioral: Clang and Buster binaries
are not expected to be byte-identical. This independent finite test strengthens
semantic evidence but is not a language-wide correctness proof, a public
cross-compiler `va_list` ABI test, or resource-header compatibility coverage.
It adds no worker fan-out. A single compiler invocation is currently serial;
test-worker settings are not evidence of compiler-lane determinism.

## Failure evidence

Each run exclusively claims a fresh directory under
`build/self-host-audit/<Config>/run-<pid>-<clock>-<attempt>/`. No stale binary,
trace or success marker can satisfy a new run. The driver writes `RUNNING`
first and `PASS` only after every gate succeeds. A failure records the earliest
unvalidated child, keeps earlier and partial artifacts, and returns failure.
Each child retains length-prefixed argv in `.command`, raw `.stdout` and
`.stderr`, and portable/native status plus timeout state in `.status`.
Comparisons report the first differing byte offset and both paths/sizes.
Writes and closes are checked; inability to retain evidence is itself failure.
The CI job uploads the evidence on success or failure for seven days.

Replay the exact argument vector from the failed child's `.command` in the
same source checkout/configuration, choosing fresh output/trace prefixes.
Investigate tokens before IR, IR before MIR, and MIR before allocation,
encoding/linking or runtime behavior. Do not disable a verifier or add an
expected-failure entry to turn an audit failure green.

## Trace format and cost

`ide cc -fbootstrap-trace=<prefix>` supports exactly one native C input and
object or executable output. It creates `<prefix>.tokens`, `.ir` and `.mir`.
The v2 envelope is an eight-byte little-endian string length, the string
`BUSTER bootstrap trace v2`, the similarly encoded phase name, explicit
semantic fields, and the eight-byte completion footer `BSTREND2`.
All scalar fields are little-endian u64; byte strings carry u64 lengths.
The producer is the field-order specification in
`src/buster/lib/compiler/codegen/bootstrap_trace.c`.

The snapshots are diagnostic semantic projections, not a replay/interchange
format. They do not serialize pointers, padding, capacity, lazy ABI caches or
physical source byte offsets. Canonical IDs/types/values/blocks/instructions,
floating-format identity, binary16 and bfloat16 target layouts,
edge arguments, symbols/globals/relocations, and selected machine instructions,
registers/blocks/edges/copy sources and side tables are explicit. The canonical
function record and selected machine function record each include the separate
exception-root block ID plus one. Line-mark instruction IDs are included.
Trace v2 intentionally omits the `IrFunction.debug_locals` and
`MachineFunction.debug_values` tables. This diagnostic projection remains
unchanged; the exception-root work does not expand its schema. Debug-bearing compiler executables are compared
in full: executable bytes are not stripped or normalized to obtain a pass.
Code-generation retry boundaries are recorded rather than silently concatenated.

Tracing is opt-in, with a checked 64-KiB buffer per stream. Ordinary compilation
does not walk IR/MIR for evidence or re-run the general MIR verifier. Traces can
be large; they are streamed, compared through read-only mappings, and retained
rather than reduced to collision-prone summary counts.

## Regression ownership

`self_host_audit_self_test` tests the native comparator, metric parser, evidence
completion, missing/empty files, child status, timeout and sanitizer handling.
Driver tests check independent traces, same-length token mutations, object
identity with tracing off/on, invalid flag combinations and writer failures.
Machine tests cover complete ordinary-branch/switch edges on both architectures,
including a minimized ternary-assignment case whose cross-block value had no
edge in the selected MIR graph.

## Post-merge failure response

Moving this check out of admission permits a fixed-point-only regression to
reach main before detection. The normal pre-merge matrix mitigates that risk
but provides no equivalent repeated fixed-point proof.

`self-host-audit-report.yml` receives completed main-push audit runs from the
trusted default branch. A non-success result, including timeout or cancellation,
opens an issue assigned to `davidgmbb`, with the exact failed SHA, run, attempt,
job-log link and stage-artifact link. It also writes a visible job summary and
retains the report for fourteen days even if issue publication fails. The
handler has issue-write access but does not check out source, execute candidate
code, read stage artifacts, modify checks or rerun validation. The original
audit status stays attached to its exact commit. The response owner's GitHub
issue-assignment/Actions notification settings control off-site delivery.

The response owner is the solo maintainer `davidgmbb`. Triage each new failure
promptly, identify its earliest failed step and preserve evidence before the
seven-day audit-artifact expiry. File the diagnosis and culprit SHA on the
automatic issue. A confirmed compiler/fixed-point defect needs a repair or
revert through normal validated PR admission. For an infrastructure failure,
record the cause before a manual retry and link the fresh attempt; keep the
original failure. Do not automatically revert an unclassified failure or retry
until green. A later passing main commit does not validate an earlier failed
commit. Cancellation/runner loss before upload can leave no stage artifact;
the completed-run handler still records the missing validation and log link.
The failure handler itself must be monitored if GitHub rejects issue creation.

## Rollout validation and bounded cost trace

Offline controls exercise failure, timeout and cancellation report bodies,
exact SHA/attempt attribution, owner assignment, retained summaries and a
failed publication's nonzero exit without creating production issues. They
also verify push-only triggers, exact checkout, unchanged native validation
steps and three overlapping main runs with distinct concurrency keys. These
controls are not a live workflow-delivery or notification receipt.

After activation, retain one PR update, one queue group's completed admission,
and two successive main pushes on #3045. For each record the exact subject SHA,
event, audit run (or verified absence), timestamps, job duration, required-check
outcomes and artifact link. Verify a controlled report failure in an isolated
Actions validation run and retain its issue/summary URL before closing #3045;
do not deliberately break compiler correctness on main.

The before-change completed audit in run 37660699713/job 112927800727 took
46m41s; run 37534735978/job 112512689016 was cancelled after its 28m02s ordinary
bootstrap and is not a completed-audit sample. These examples establish cost
and variance, not a measured after-change saving. Compare the bounded after
trace's removed PR/queue executions and runner-minutes with its actual
pre-change counterparts; measure admission critical-path latency separately.
The main audits still incur their full cost. Savings and queue latency impact
remain unmeasured until that deployment trace exists.
