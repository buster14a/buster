# Sealed raw compiler evidence v1 (#2918)

Status: the ordinary pull and main routes seal their raw evidence, and their
publishers verify it natively. Sealing is not yet required. An artifact with
neither control file is `legacy-unsealed`: its outcome stands, but its
retained raw bytes are unverified, never upgraded. Requiring v1 on the
ordinary routes is the next step on #2918.

## Producer and publisher

`compiler_compare.py --request-attempt N` marks an ordinary attempt for
sealing. That excludes the owned, analyzer and snapshot routes. For such an
attempt:

- It exports the lab and corpus with this inventory as required.
- `export_tree` copies required members of a whole tree before optional ones.
  An optional member must also leave 4 MiB and 32 entries for later trees'
  required members.
- The exports stop 6 MiB and 3 entries short of the sealed bounds, so the
  control files always fit.
- Every omission is recorded with its fixed reason code.

Once the attempt is measured, the producer writes `evidence-omissions.json`
and runs the trusted `./build.sh compiler_evidence seal` on the bundle, with
the receipt's own identity. Only then does it write the final receipt, whose
`evidence_bundle` names the seal: members, payload bytes, manifest bytes and
SHA-256, and the omission count. A missing required member, or a refused
seal, fails the attempt.

`compiler_publish.py` handles each artifact as follows:

- **Neither control file:** the artifact is `legacy-unsealed`. The outcome
  stands, and the report notes that the raw bytes are unverified.
- **Manifest or seal present:**
  1. It reads the whole archive through the strict `sampling_archive` reader
     and extracts it.
  2. It runs `./build.sh compiler_evidence verify` under the identity that
     hosted authorization produced; the receipt's identity is not used.
  3. A refusal fails the check.
  4. A verified bundle must match the receipt's `evidence_bundle`.
- **Publication-only recovery:** it notes a sealed bundle without
  re-verifying it.

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

## Files

`evidence-manifest.tsv` (schema `buster-compiler-evidence-manifest-v1`) starts
with `key<TAB>value` identity lines, in this order: `schema`, `inventory`,
`repository`, `request_run`, `request_attempt`, `executor_run`,
`executor_attempt`, `trusted_revision`, `baseline_revision`,
`candidate_revision`, `baseline_tree`, `candidate_tree`, `lab_profile`,
`corpus_profile`, `corpus_harness_revision`. Then come `members` and
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
`corpus_harness_revision`.

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

It also checks that a `machine-specifications/` record appended after sealing
still verifies. Linux is the only supported platform; other driver builds
report it as unsupported.
