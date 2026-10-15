# Sealed raw compiler evidence v1 (#2918)

Status: the ordinary pull and main routes seal their raw evidence, and their
publishers verify it natively and replay the lab and corpus from it; that
includes the pull `snapshot-v1` route. Sealing is required: a measured
ordinary attempt without both control files fails. Only publication-only
recovery of a run whose producer predates sealing may be `legacy-unsealed`:
its outcome stands, but its retained raw bytes are unverified, never upgraded.

## Producer and publisher

`compiler_compare.py --request-attempt N` marks an ordinary attempt for
sealing, on the `legacy-rebuild` and the non-owned pull `snapshot-v1` route
(#3211) alike. The utility-owned, MAIN-owned and analyzer routes keep their own
contracts and are not sealed. For such an attempt:

- It exports the lab and corpus with this inventory as required.
- In such an attempt every `export_tree` call, including those of the opt-in
  legs that run before the lab, exports only required members while its leg
  runs, and creates
  a directory only as the ancestor of an exported file. Empty or unexported
  optional subtrees therefore never use the entry budget.
- Once every leg's required members are in place, `export_deferred_optional`
  adds the optional members within what is left. It takes the trees in export
  order, and each tree's members in bytewise path order. An earlier leg's
  optional data can never starve a later leg's required members.
- A leg may still read a member it did not list as required, for example a
  part exported with no required tuple. In that case `read_exported_json`
  exports that one member first, within the remaining budget, using the same
  no-follow walk. Validation therefore never sees a deferred member as
  missing.
- The exports stop 6 MiB and 3 entries short of the sealed bounds, so the
  control files always fit. Entries count directories as well as files, as
  the verifier does.
- Every omission is recorded with its fixed reason code.

Once the attempt is measured, the producer writes `evidence-omissions.json`
and runs the trusted `./build.sh compiler_evidence seal` on the bundle, with
the receipt's own identity. Only then does it write the final receipt, whose
`evidence_bundle` names the seal: inventory, members, payload bytes, manifest
bytes and SHA-256, and the omission count. A snapshot attempt seals after its
native phase owner has finished, so no owned phase follows the seal. A missing required member, or a refused
seal, fails the attempt.

`compiler_publish.py` handles each artifact as follows:

- **Neither control file:** a measured attempt fails, since v1 is required.
  Only publication-only recovery may accept it, and only when the original
  run's trusted `compiler_compare.py` has no `seal_evidence` step
  (`sealing_required`). That artifact is `legacy-unsealed`: the outcome
  stands, and the report notes that the raw bytes are unverified. An
  unreadable or absent producer requires the seal.
- **Manifest or seal present:**
  1. It reads the whole archive through the strict `sampling_archive` reader
     and extracts it.
  2. It runs `./build.sh compiler_evidence verify` under the identity that
     hosted authorization produced; the receipt's identity is not used. Only
     the [route lines](#snapshot-route) come from a `snapshot-v1` receipt.
  3. It replays the verified bytes, as described below.
  4. A refusal at any step fails the check.
  5. A verified bundle must match the receipt's `evidence_bundle`, inventory
     included.
- **Publication-only recovery:** it verifies the same way. The request attempt
  comes from the original bench run's own run-name
  (`9700X request <run>.<attempt> head <sha>`), and that run must be the
  authenticated request run. A run-name that names another request run, or no
  run-name at all, cannot name a sealed bundle, so the bundle is refused.

The report's note names the verified manifest and the replayed population:
`raw evidence: sealed … verified natively, … replayed N lab pairs and M corpus
cases`.

## Replay

Replay recomputes each saved verdict from the verified raw bytes with the
trusted statistics. It never runs an artifact's executable or command, and
it never measures. It is the same on both inventories, since the
[snapshot inventory](#snapshot-route) carries every ordinary member.

**Lab (`sealed_lab_replay`).**

1. `lab/compare.json` must be the frozen `compiler-compare-v1` configuration:
   - CPU 2, a 10-minute target, one warmup and no profile steps
   - no fixed pair count and no flag pair
   - an even ABBA plan of 10 to 1000 pairs, with exactly two `pairs.json`
     records per pair
2. Each variant's saved runs, failures, identical runs, path, hash and size
   must follow from those records.
3. Each record must be the next member of the declared ABBA order. It must
   have exited 0, be deterministic and have a bounded span. Its counter state
   must equal its variant's actual probe, and every counter or
   compiler-metrics file its availability claims must be present. This
   rejects duplicated, missing, failed and reordered members, which
   `uarch_lab`'s own loader would silently accept.
4. The trusted `uarch_lab` statistics then recompute the metrics, phases,
   checks and verdict, and these must equal the saved summary
   (`lab_raw_inference`, shared with the utility route).

The cross-variant output and code-size facts read the unretained reference
images, so they are not replayed. As accepted, a sealed bundle supports no
code-size claim.

**Corpus (`sealed_corpus_replay`).**

1. The publisher copies `throughput/` to a disposable directory, because
   `bench_throughput compare` rewrites its summaries.
2. It runs its own checkout's `./build.sh bench_throughput compare --output
   <copy>`. That checks the completion hashes and the sample population, and
   recomputes the statistics.
3. The regenerated `summary.json`, and `summary.md` when retained, must equal
   the retained bytes. A valid regression stays report-only.

The comparison has been byte-stable from the pinned historical harness
`afe67cbe…` onward. A future change to its output, or to its result schema,
therefore needs a reviewed replay transition before it lands on `main`.

`publish-pull` bootstraps the driver with the pinned TinyCC, as
`publish-compiler` does.

## Scope

Contract v1 covers the ordinary and pull 9700X compiler comparison:
the stage-1 self-host lab (`lab/`, profile `compiler-compare-v1`) and the
frozen corpus (`throughput/`, profile `throughput-corpus-v2` or the named
`throughput-corpus-historical-v1` recipe). Opt-in legs, scaling, sampling,
preparation, utility and analyzer bundles keep their own contracts. A sealed
bundle supports no code-size claim.

The required inventory is `buster-compiler-ordinary-raw-v1`:

| Area | Required members |
|---|---|
| Control | `evidence-omissions.json` |
| Lab plan and verdict | `lab/compare.json`, `lab/summary.json`, `lab/pairs.json`, `lab/a/lab.json`, `lab/b/lab.json`, `lab/a/env/env.json` |
| Lab logs, both variants | `lab/{a,b}/` `commands.log`, `wrapper-rss.log`, `source-run.log`, `metrics-probe.log`, `warmup-0.log` |
| Lab pairs | `lab/pairs/NNNN-v.csv` when variant `v` reports `capabilities.perf_stat.usable`, and `lab/pairs/NNNN-v.ccmetrics` when it reports `collection.metrics_out`, for every pair `0001..` of `plan.pairs` |
| Corpus | `throughput/` `samples.csv`, `telemetry.csv`, `metadata.json`, `jobs.tsv`, `commands.jsonl`, `capabilities.jsonl`, `complete.txt`, `summary.json`, `cpuinfo.txt` |
| Corpus inputs | `throughput/inputs/<workload>.c` for each of the six frozen workloads |

`plan.pairs` must be an even integer from 10 to 1000. Every other retained file,
such as the lab report or per-run corpus logs, is `optional`. Roles come from
this table and the bundle's own plan and availability records, never from a
producer's role column.

Two points differ from the original proposal, because the proposal did not
match the producer:

- `machine-specifications/` is appended by a workflow step after the producer
  exits, so it is not a manifest row. The publisher keeps validating it with
  `compiler_publish.machine_record`.
- `uarch_lab.py` writes `env/env.json` only for variant `a`, so only
  `lab/a/env/env.json` is required.

### Snapshot route

The pull `snapshot-v1` route (#3211, [compiler-closure-v1.md](compiler-closure-v1.md))
runs its children under the trusted native phase owner and adds its own raw
evidence beside the receipt. Its inventory is `buster-compiler-snapshot-raw-v1`:
every `buster-compiler-ordinary-raw-v1` obligation above, plus these.

| Area | Required members |
|---|---|
| Closure records | `closure-snapshot.json`, `closure-restore.json`, `closure-verify.json` |
| Closure manifests | the same three, each with `.manifest.tsv` appended |
| Owned phases | for each phase `NNNN` from `0001` to the trusted `owned_phases` count: `owned-phases/NNNN.json` and its `.argv`, `.stdout`, `.stderr` and `.bootstrap.complete` |

The closure records and manifests are what the publisher's `validate_closure`
replays. The phase members are the native ownership, command, output and
bootstrap proof of every measured child; an empty `.stdout` or `.stderr` is a
required zero-byte row. `owned-phases/` must hold exactly that population: a
member of a phase past the count, or any other file there, refuses the bundle.
`closure.log` and every other file stay optional. These members count against
the same retained bounds; no bound changes for the route. The hosted fixture's
attempt has 14 phases, 70 entries, but a lab plan near 1000 pairs with both
streams on both variants plus a large phase population can exceed 4096
entries. Its seal is then refused and the attempt fails, as on the ordinary
route.

The route is selected by two lines after the ordinary identity, and only by
them. A `legacy-rebuild` identity has none, so its manifests are unchanged.

```text
preparation_policy<TAB>snapshot-v1
owned_phases<TAB><1..256, no leading zero>
```

The producer and the publisher both take these from the receipt
(`compiler_receipt.evidence_identity`): its declared `preparation_policy` (or
`snapshot-v1` when it carries a `closure` record without one, as
`validate_closure` reads it) and its `phase_ownership.count`. The count is the
native owner's bound of at most 256 phases. A snapshot identity with the
historical corpus is refused, because the route runs only
`throughput-corpus-v2`. The receipt cannot weaken the route:

- A receipt that declares `legacy-rebuild` but carries a snapshot closure fails
  `validate_closure`.
- A snapshot bundle verified under the ordinary identity, or under another
  phase count, does not reproduce its manifest.
- A snapshot receipt without a valid count names no sealed identity, so its
  bundle is refused.

The ordinary pull reader also loads each declared phase's five members, as the
owned routes' readers do, so `validate_closure` replays the population at
publication. A missing or oversized member is a refusal.

## Files

`evidence-manifest.tsv` (schema `buster-compiler-evidence-manifest-v1`) starts
with `key<TAB>value` identity lines, in this order: `schema`, `inventory`,
`repository`, `request_run`, `request_attempt`, `executor_run`,
`executor_attempt`, `trusted_revision`, `baseline_revision`,
`candidate_revision`, `baseline_tree`, `candidate_tree`, `lab_profile`,
`corpus_profile`, `corpus_harness_revision`, then, on the snapshot route only,
`preparation_policy` and `owned_phases`. Then come `members` and
`payload_bytes`. After a `role bytes sha256 path` column line, there is one row
per retained file, in bytewise path order. The historical profile must name
the pinned harness revision `afe67cbe…`.

The manifest omits these files:

- `receipt.json`
- the manifest itself
- `evidence-seal.txt`
- everything under `machine-specifications/`

`evidence-seal.txt` (schema `buster-compiler-evidence-seal-v1`) holds the
manifest's byte count and SHA-256, and is written last.

`evidence-omissions.json` (schema `buster-compiler-evidence-omissions-v1`) has
exactly four keys:

- `schema`
- `total`
- `by_reason`: counts for `excluded-type`, `member-limit`, `byte-budget`,
  `file-budget`, `changed-during-copy` and `read-error`. The counts must sum to
  `total`.
- `examples`: at most 50 `{path, reason}` objects.

A required member may never be omitted.

## Bounds and path rules

| Bound | Value |
|---|---|
| Retained bytes | 48 MiB in total |
| Retained entries | 4096 directories and files, control files included |
| Control reserve | 6 MiB and 3 entries kept for the control files when the producer exports (#2918 producer step) |
| Member size | 8 MiB for a `.json` member, 32 MiB for any other member |
| Manifest | 4 MiB |
| Receipt | 1 MiB |

Paths are relative, at most 512 bytes, and made of printable ASCII only. They
must not contain any of the following:

- `.` or `..` segments
- empty segments
- backslashes

An entry whose name starts with `.` is not retained, and no manifest row
names it: `actions/upload-artifact` drops hidden files, including the
producer's scratch marker. The `machine-specifications/` subtree is not
walked, because the publisher validates it separately.

Every other directory and file is retained. Control files count too. Each
retained entry counts against the entry bound and must pass the path rules.

The walk refuses:

- symlinks and any other non-regular file
- any two retained names, a directory, control file or `machine-specifications`
  included, that collide after case folding

## Native interface

```text
./build.sh compiler_evidence seal BUNDLE IDENTITY
./build.sh compiler_evidence verify BUNDLE IDENTITY
./build.sh compiler_evidence self-test
```

`IDENTITY` is a file of the trusted identity lines, from `repository` through
`corpus_harness_revision`, plus the two route lines of a snapshot attempt.

`seal` writes the manifest and then the seal. It refuses when:

- either control file already exists
- a required member is missing
- the plan, availability records or omissions document is malformed
- the manifest, the seal and a receipt at its 1 MiB bound would not fit the
  byte and entry bounds; this is checked before anything is written
- the retained set changes while sealing

Once either control file is written, any later failure removes both, so a
failed seal never leaves a completion marker behind.

It prints `COMPILER_EVIDENCE_SEALED` or
`COMPILER_EVIDENCE_INCOMPLETE reason=…`.

`verify` regenerates the manifest from the retained bytes under the trusted
identity. It requires that regenerated manifest to match the retained one byte
for byte, and the seal to bind it. It prints
`COMPILER_EVIDENCE_VERIFIED members=… payload_bytes=… manifest_sha256=…` or
`COMPILER_EVIDENCE_REFUSED reason=…`.

The self-test covers the normal seal and verify, and these refusals:

- identity mismatch, and a historical identity without the pinned harness
- a raw byte, removed, added, relabelled or availability tamper
- a seal or receipt tamper
- a summary-only bundle
- bad plans and bad omission documents
- symlinks, case aliases and malformed paths
- on the snapshot route: malformed route lines (count 0, 257, a leading zero,
  a policy without a count or the reverse, the historical corpus), an ordinary
  bundle or a short population sealed as snapshot, an extra or unknown phase
  member, a tampered or removed closure record or manifest, a removed phase
  member, changed phase output, and verification under the ordinary identity
  or another phase count

It also checks that a `machine-specifications/` record appended after sealing
still verifies. Linux is the only supported platform; other driver builds
report it as unsupported.
