# Retirement source preparation (#1018)

The private `bq_retirement_preflight` entry accepts only the compiled recipe's
`inventory-sha256` pin and the operator-installed inventory. Its two requested
commit IDs must match that inventory exactly; the archived direct-native and
MIR candidate commits **and trees** must differ. Each immutable source manifest
must enumerate the entire installed directory tree, including no unlisted
files, empty directories or links. The preflight hashes every listed file and
checks the inventory's exact entry count, byte count, directory count, longest
path and depth. These are measured from the installed closure, not a capacity
increase inferred from a miniature fixture.

`bq_retirement_verify_subject` checks the first materialized source, creates a
second independent copy, rehashes that copy and the installed source, then
removes only the held verification tree. The durable `BQ-RETIREMENT-PREP-V2`
record retains the pinned source identities and distinct installed/copy inode
closures. `bq_retirement_preparation_ready` reopens the installed inputs and
both materialized subjects before any consumer can use that record. The
matched trusted build has a separate private begin/stage/launch/complete/import
sequence; the preparation record itself does not claim an executable was built.

Before copying, the preflight derives the following **source-only** storage
bound from both scanned manifests and the actual workspace filesystem:

```
payload = sum(source bytes + source manifest bytes) for both subjects
nodes = 8 + sum(source entries + source directories + 1 manifest) for both
unit = max(workspace f_frsize, workspace f_bsize, 8192 bytes)
source_reservation_bytes = 2 * payload + 2 * nodes * unit + 1 MiB
```

Every arithmetic operation is checked for overflow. It requires at least
`ceil(source_reservation_bytes / f_frsize)` available blocks and `2 * nodes`
available inodes. The two-copy bound covers the peak when both first copies
exist and one independent verification copy is being built; it also includes
directory metadata and the preparation record margin. Missing filesystem
capacity fails before materialization. The record retains the computed byte
bound, and import recomputes it from the installed sources on the same
workspace filesystem. A changed block geometry or modified inventory cannot
silently validate the old record.

This capacity does not reserve trusted build outputs, complete #508 requested
work, result shards or an export archive. The public
`native-retirement-performance-v1` descriptor remains blocked and has no
reviewed full inventory pin. An actual #508 workload/dependency/SDK/sysroot
closure, independently verified Git commit/tree relations, matched uninstrumented
Clang binaries and their output storage, and integrated B/worker gates must be
published and tested before #881 recipe admission. The private fixture checks
the mechanics using miniature sources; it is not full-population evidence.
