# Desktop producer handoff screening (#886)

Status: rejected at the bounded screening gate.

Evaluated against `main` commit
`82a63c5110aa63c3fb0e03d045a49343148ca787`. The frozen performance anchor is
workflow run `35471925985`, attempt 1.

## Scope and acceptance gate

Issue #886 asks for one producer class that is identical between the paired
`release` and `checks` desktop shards, can be published early enough to preserve
parallelism, and improves the frozen #709 anchor by at least 10%.

The anchor completed in 24 minutes 48 seconds (1,488 seconds). The minimum
accepted saving is therefore 148.8 seconds.

This screening uses zero transfer overhead as an intentionally optimistic upper
bound. A candidate that cannot meet the gate with zero upload, artifact wait,
download, verification, and extraction cost cannot become eligible by adding
those costs.

## Candidate inventory

| Producer class | Complete identity across paired shards? | Observed duplicate work | Decision |
| --- | --- | --- | --- |
| Compiler build trees | No | `release` owns canonical unsanitized Clang Release; `checks` owns sanitized Clang Debug/Release and non-Clang Debug rows. | Ineligible: compiler identity, configuration, flags, sanitizer state, and payload differ. |
| Verified Zig setup, Intel macOS | Yes, as a setup input | 5.527 seconds for 109,339,525 bytes | Reject: the zero-overhead upper bound is 3.7% of the required saving; transferring 109.3 MB adds work. |
| Verified Zig setup, Windows x86-64 | Yes, as a setup input | 6.109 seconds for 97,217,739 bytes | Reject: the zero-overhead upper bound is 4.1% of the required saving; transferring 97.2 MB adds work. |
| CI bootstrap build driver | Potentially, within the same platform and toolchain | About 2.3 seconds on Intel macOS; similarly only a few seconds on Windows | Reject: artifact publication and consumption cannot produce a 148.8-second critical-path saving. |

### Compiler trees

The shard plan intentionally avoids duplicate compiler producers. `release`
owns the canonical unsanitized Clang Release tree. `checks` owns sanitizer and
non-Clang rows. Their complete identities are not equal, so a consumer could not
accept a release tree without weakening the attestation contract. Reassigning a
row would change the shard plan and risk serializing the paired jobs instead of
eliminating duplicate work.

### Zig setup

Both paired jobs already restore and verify Zig from the runner-local cache. The
largest observed eligible setup cost is 6.109 seconds. A same-run handoff would
replace that local operation with producer upload, artifact-availability wait,
consumer download, manifest validation, payload hashing, and extraction of
97-109 MB.

Even an impossible zero-cost transfer saves at most 6.109 seconds: 0.41% of the
1,488-second anchor and 4.1% of the required 148.8-second improvement. Real
transfer overhead can only reduce that value.

### Bootstrap driver

The shared bootstrap executable is cheap to rebuild from checked-out source
with the platform compiler. On Intel macOS the observed compile interval was
about 2.3 seconds. Creating a same-run artifact dependency for this payload
would add more coordination and trust surface than the rebuild it replaces.

## Attestation and trust disposition

A compliant handoff would need to bind and independently reconstruct every
field required by #886: source and dependency digests; artifact kind and schema;
compiler identity and content digest; target, CPU, configuration, generator,
flags, and environment inputs; runner and toolchain identity; payload path,
mode, size, and digest; workflow run, attempt, and producer shard; and manifest
digest. Any mismatch must fail closed with no fallback.

No such path is added because no candidate survives the zero-overhead economic
gate. Avoiding unused artifact plumbing also preserves the existing provenance
checks and parallel job graph.

## Decision

Reject the producer handoff on the current shard architecture.

- No compiler tree has equal complete identity across `release` and `checks`.
- The largest eligible duplicate producer costs 6.109 seconds, versus a required
  148.8-second workflow improvement.
- The 12 desktop shards and the 23-job trust and completion contract remain
  unchanged.
- #885's matched-cohort machinery is not required for this rejection: the
  candidate fails an optimistic analytical upper bound before measurement noise
  or cache population can affect the result.

Re-open the design only if a future shard plan introduces a producer with
complete identity equality and a measured critical-path rebuild cost above the
full publication and consumption cost by enough to clear the frozen gate.
