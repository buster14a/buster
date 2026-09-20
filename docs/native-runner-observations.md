# Native runner observations

The six native desktop jobs keep correctness ownership in `build.c` and evidence
packaging ownership in `tools/ci_pack_evidence.py`.  Native runner observation is
a narrow telemetry layer around those existing commands.  It does not choose a
faster attempt, retry a slow attempt, cancel a slow attempt, change a required
step, or turn a correctness failure into a success.

## Artifact contract

Each native job initializes one immutable observation root at
`$RUNNER_TEMP/buster-ci/native-observation`.  The pre-pack records are included
in the existing `native-ci-logs.tar.gz` archive.  After the archive attempt,
`tools/ci_native_observation.py finalize` writes the authoritative
`native-observation.json` both into that root and beside the archive,
`result.json`, and `summary.md` in the directory passed to
`actions/upload-artifact`.  A packing failure therefore leaves the final
incomplete observation in the existing unpacked fallback path.

The versioned records are:

- `buster.native-observation.identity.v1`: source commit and tree, workflow and
  run identity, job kind, requested runner label, hosted image identity,
  operating-system and architecture identity, CPU signature, core and RAM
  counts, and the effective configuration and worker budget;
- `buster.native-observation.toolchain.v1`: resolved compiler, CMake, and Ninja
  paths plus the SHA-256 of each complete version response;
- `buster.native-observation.phase.v1`: one monotonic interval, status, exit
  code, command digest, UTC endpoints, and measured durable-record write cost;
- `buster.native-observation.v1`: ordered phase records, completeness and
  correctness disposition, the exact upload input manifest, and aggregate
  observer overhead;
- `buster.native-observation.enriched.v1`: the artifact joined to immutable
  GitHub job-log and Actions API evidence; and
- `buster.native-observation.classification.v1`: an exact-identity comparison
  result and per-phase ratios.

The process-local artifact cannot see the hosted provider preamble that GitHub
writes before the job process starts.  Provisioner version, Azure region,
runner version, worker ID, and the numeric Actions job ID therefore remain a
separate evidence source.  `enrich` joins them to the artifact through the
exact repository, source, workflow run, run attempt, and job identity.  Missing
or contradictory provider identity makes the sample non-comparable; it is
never guessed from the requested label.

## Timed phases

Unix native jobs retain fixed CPU and filesystem calibration probes and time:

1. build-driver bootstrap;
2. CMake configuration;
3. the Release `ide` producer build;
4. execution-mode payload work after that producer exists;
5. differential-driver refresh and differential self-test/preparation;
6. the no-op producer availability check used by the differential gate;
7. the full native differential corpus;
8. native evidence packing; and
9. validation and digesting of the exact directory handed to
   `actions/upload-artifact`.

Windows jobs retain the same calibration, bootstrap, configuration, producer,
mode-payload, packing, and handoff phases.  They do not invent differential
work that the Windows native lane does not own.

The explicit producer build does not add a second producer.  The mode command
already required the same target; making it explicit moves that cost out of the
mode-payload interval and the subsequent mode target observes a current Ninja
graph.

`time.perf_counter_ns()` supplies every command interval.  UTC timestamps are
stored only as independent ordering evidence.  GitHub log timestamps and
Actions API timestamps remain separate in the enriched record.  A backwards
log clock or a material API/log disagreement is retained and makes the sample
non-comparable rather than being normalized.

The CPU probe hashes a fixed 8 MiB stream.  The filesystem probe durably writes,
reads, verifies, and removes a fixed 8 MiB payload.  Every phase record reports
the measured cost of its first durable atomic publication.  Finalization sums
those costs and publishes them in both JSON and the step summary; it does not
subtract them from measured commands.

## Failure and cancellation

The command wrapper forwards `SIGINT` and `SIGTERM` to the complete child
process group, waits for it, and emits a failed or cancelled phase record with
the original exit status.  The canonical record is also printed as a
`NATIVE_PHASE_RECORD` line, so an immutable job log still contains the
observation when the repository's existing policy suppresses new artifact
uploads after cancellation.

A successful native payload fails closed when required timing evidence is
missing, duplicated, malformed, identity-mismatched, non-monotonic, or
unsuccessful.  A failed or incomplete payload may still publish a structurally
valid observation, but its correctness outcome is `non-success` and it is never
eligible for performance comparison.  Packing and retention use `always()`
conditions after checkout and initialization: a packing failure keeps the
existing unpacked fallback, the partial packed directory, all phase records,
and the final incomplete observation.  Cancellation still cannot be made
reliable after the hosted runner itself is forcibly reclaimed, so the canonical
`NATIVE_PHASE_RECORD` line in the immutable job log remains the final fallback.

## Exact-match classification

Classification requires one target and at least two complete, successful
comparators.  The source commit and tree, workflow definition, job kind,
requested label, hosted image, runner version, provisioner, Azure region,
CPU/RAM identity, compiler/CMake/Ninja identities, configuration, and worker
limits must all match exactly.  Run and attempt IDs stay in every record but
are intentionally different across observations.

The current broad-degradation policy is frozen as:

- slow phase: target duration is at least `1.50x` the comparator median;
- broad degradation: at least four independent phases are slow;
- at least one slow phase must be setup/build work; and
- `differential_corpus` must also be slow.

One slow LLVM decode, one slow producer build, or one slow packing phase can
never classify the whole hosted runner as degraded.  The classifier emits one
of the four predeclared dispositions:

- `hosted-runner-wide` when the target alone meets the broad rule;
- `source-specific` when both exact-match reruns reproduce every phase in an
  explicitly supplied, predeclared broad slowdown signature;
- `timestamp-invalid` when raw-log ordering is contradictory or materially
  disagrees with Actions API metadata; or
- `mixed/inconclusive` for insufficient comparators, non-comparable identity,
  incomplete evidence, command/sequence conflicts, or a non-broad result that
  does not reproduce the complete signature.

The optional slowdown signature is not inferred after looking at the cohort.
It must contain at least four phases, include setup/build work, and include
`differential_corpus`; otherwise classification fails closed.

## Audit commands

Download the native artifact, decoded job log, and the JSON response from the
Actions job endpoint without rewriting any timestamps.  Then run:

```sh
python3 tools/ci_native_observation.py enrich \
  --observation native-ci-upload/native-observation.json \
  --job-log job.log \
  --job-metadata job.json \
  --output enriched.json

python3 tools/ci_native_observation.py classify \
  --target target-enriched.json \
  --comparison comparator-2-enriched.json \
  --comparison comparator-3-enriched.json \
  --slowdown-phase configuration \
  --slowdown-phase producer_build \
  --slowdown-phase mode_payload \
  --slowdown-phase differential_corpus \
  --slowdown-phase evidence_packing \
  --output classification.json
```

All failed, cancelled, incomplete, identity-mismatched, and clock-inconsistent
attempts stay in the audit ledger.  They are not silently replaced by a later
attempt.
