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
rules and contain positive counted confirmed regressions. Both exported raw
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
also exercises a complete corpus regression with real exit 1 and three
incomplete exit-1 cases, each proving no later child and retained failed data.
These are functional diagnostics and never performance qualification.
