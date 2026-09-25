# Constrained systemd broker for Benchpress

This is a review and operator reference for issue #880. The broker is a separate
Linux C executable built from `tools/bench_service/systemd_broker.c`. The
long-lived service and fixed build driver call its unprivileged CLI. A root
socket instance checks `SO_PEERCRED` and accepts only the `buster-bench` UID.
It receives a typed operation, positive job/attempt numbers, a fixed stage and
two full lowercase source revisions. The broker creates its own child pipes,
relays bounded stdout/stderr and returns the exact exit status over the socket;
it never passes caller-owned file descriptors into a candidate unit. The server constructs
every `/usr/bin/systemd-run` or `/usr/bin/systemctl kill` argument from
constants. There is no arbitrary argv, environment, executable, unit name,
property, shell command or path in the protocol and no direct-manager fallback.
Read-only `systemctl show` remains in the coordinator for its existing unit and
cgroup verifier.

The broker checks the service-owned worker record and, for stages and signals,
the instance record; the recorded boot and job/attempt must match. It requires
the stable lease to be locked and checks its device/inode against a root-owned
receipt. Starts compare the installed and materialized source manifests and
require root-owned installed executables. Signals first inspect the exact unit,
slice, identity, executable, resource settings and, for the outer unit, the
recorded invocation and cgroup. A mismatch denies the request. The coordinator
continues to own the complete cleanup, quarantine and recursive cgroup proof.
For `Slice=buster-bench.slice`, the effective control-group path is nested
under `/buster.slice/buster-bench.slice`; both broker and worker verify that
actual hierarchy.
The only resume signal is `CONT` for the exact outer instance; stage units
accept `TERM` or `KILL` only.

The template retains root UID with empty capability sets. Its primary group
is `buster-bench` and its supplementary group is `buster-bench-candidate`.
Systemd may also retain root's group 0 in the effective supplementary set;
the server permits only group 0 and those two fixed groups and requires the
candidate group to be present. Read back the effective set during review.
The broker can traverse the service-group-only queue, lease and results
parents and the candidate-group workspace ancestry. Worker records are
`0440` service:service, the stable lease is `0640` service:service, and each
durable result leaf remains `0700` service:service. The broker opens private
directory components with `O_PATH`, checks the exact owner/group/mode, and
reads only the named records and manifests. Candidate and runner accounts
have neither service-group membership nor access to the socket.

These are group-scoped filesystem grants, not per-request capabilities. The
broker's code restricts which named objects and manager operations a request
may use; membership can also read other reachable group-readable objects.
`ProtectSystem=strict` and the explicit read-only state mount keep the broker
from writing candidate-group staging directories. Cleanup is the trusted
service's responsibility: the foreign-owner exception requires the resolved
non-root `buster-bench-candidate` UID and primary GID, the same attempt GID,
and mode `0770` or `02770`. Another UID with that GID is an identity mismatch,
not a candidate payload. A missing/aliased candidate account disables this
exception; service-owned cleanup remains available.

## Build and review

From the reviewed checkout, run `./build.sh bench_service_broker self-test` and
`./build.sh bench_service self-test`. The former constructs all six fixed
launches and exercises malformed request rejection without manager access.
Review the broker source, this packet, the socket/template service, tmpfiles,
and the changed service/build-driver call sites as one transition. Build the
installed service, build driver, throughput tool and broker from one selected
protected-main commit. Compile the installed `build.c` recipe driver with
Clang and check that its ELF `GNU_STACK` program header is `RW` without `E`.
The TCC bootstrap driver has no such header and fails at stage `posix_spawn`
under `MemoryDenyWriteExecute=yes` with `EACCES`; it is not an installable
recipe driver. Record SHA-256 of all binaries, dependencies, units and
the complete immutable source inventory. The broker must be root-owned,
executable and unwritable by service, candidate and runner accounts.

One reviewed local build of the installable driver is:

```sh
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable \
  -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o build/buster-bench-build
readelf -W -l build/buster-bench-build
```

Read back the `GNU_STACK` line as `RW` with no `E`, then hash the binary that
is actually installed. This command produces an installation candidate; it
does not replace the TCC bootstrap used by `build.sh`.

`./build.sh bench_service_broker self-test` also builds
`build/bench-service-tools/systemd-broker-live-test` from
`tools/bench_service/systemd_broker_live_test.c` with the broker's warning and
integer flags. In a disposable, provisioned real-systemd
container, start the reviewed service, submit one exact fixed-recipe job, and
run that test as root while its outer unit is active with
`BUSTER_BROKER_LIVE_TEST=1` and arguments `JOB ATTEMPT BASE_REVISION
CANDIDATE_REVISION`. It starts the constrained broker socket and makes an
exact outer `CONT` request. It checks queue/result/lease modes and records,
then rejects candidate and runner peers, wrong instance, wrong source and
unlisted signal requests. It offers a valid request as root and requires
a complete server rejection frame on that same connection, including when
the broker rejects the peer before reading the request; candidate and
runner must instead receive `EACCES` or `EPERM` from their own socket
`connect` calls. A missing endpoint, refused connection or malformed response
fails the probe. The root preflight verifies the real service-owned socket
inode and mode `0600`, since an inaccessible parent could otherwise yield
`EACCES` even when the socket itself is absent. Its container and environment
gates prevent an ordinary host test invocation.

Account resolution uses the full supplementary membership list, not just
primary GIDs. Candidate and runner must not have the service or root group;
the runner must not have the candidate group. Each probe child calls
`initgroups`, drops all three UID/GID slots, sets `NoNewPrivileges`, and reads
back its complete effective group set against the captured account inventory.
A membership change between capture and execution fails the probe.
Candidate/runner children try read-only, write-only and read-write opens of
both worker records and the lease; no create, truncate or write is attempted.
They separately test listing and searching the queue, lease and result
directories. Before dropping credentials, the probe verifies an existing
service-owned regular `payload` in the result leaf with mode `0400` and one
link. Candidate and runner then attempt read, write and read-write opens of
that exact file without creating, truncating or modifying it. Provision the
payload as part of the disposable fixture before running either live or
`--isolation-only`; the probe does not create it. A missing payload fails the
fixture rather than counting as a denial. Only `EACCES`/`EPERM` counts as
denial. Remove the transient `payload` through the fixture owner after the
probe and before worker finalization: the production result-bundle validator
rejects unindexed extra files. If that cleanup cannot be proved while the
outer unit is active, use a separate disposable attempt for this probe and
do not count that attempt as a normal #880 qualification result. Never leave
the fixture in a protected-host result. Metadata-only `O_PATH` access to a
leaf is not treated as traversal.

`BUSTER_BROKER_LIVE_TEST=1 systemd-broker-live-test --isolation-only JOB ATTEMPT`
performs just the account/private-hierarchy checks in a disposable provisioned
container, without starting the socket or contacting the manager. Repeat with
an intentionally service-group-member candidate and runner: each must fail.
This is DAC/credential evidence, not a live broker or materializer/recipe run.
Before deployment, also read `/proc/PID/status` for the actual service,
candidate stage and runner processes: account snapshots do not prove that an
already-running unit has no additional groups from unit-specific settings.

The regular `bench_service_broker self-test` now runs both the existing command
construction test and the probe's unprivileged `--self-test` identity-policy
controls. Neither replaces the provisioned live regression. On disposable
root-capable test infrastructure with the three fixed accounts provisioned,
`build/bench-service-tools/service-tests --cleanup-identity-only` exercises the
real cleanup traversal under the service UID, including exact candidate
positives and same-GID foreign-owner negatives. It uses only fresh `/tmp`
fixtures, not the installed queue or lease.

## Host installation boundary

Keep dispatch false, the long-lived service stopped and all prior outer/stage
units and cgroups absent while installing. Preserve the existing lease file;
never recreate, truncate, rename or replace it. After a fresh no-follow `stat`
of the single-link service-owned `0640` lease, install a root-owned `0444`
regular, single-link `/etc/buster-bench/systemd-broker-lease.identity` in a
root-owned non-writable directory with exactly these two lines, using the
host's actual decimal values:

```text
device=40
inode=194974
```

The example values above are the Benchpress readback on 2026-09-24, **not** a
portable default. Recheck the inode and device immediately before writing the
receipt and after installation. The broker refuses an absent, malformed or
mismatched receipt. Never change that receipt to accommodate an unexpected
lease replacement; investigate and quarantine instead.

Install `buster-bench-systemd-broker.socket` and
`buster-bench-systemd-broker@.service` with the exact reviewed bytes. The
runtime directory is `root:buster-bench` `0710`; the socket is
`buster-bench:buster-bench` `0600`. The template process runs as root with
service primary group, candidate supplementary group, empty capability sets,
no new privileges, a read-only filesystem view and only local
Unix sockets. The long-lived service requires the broker socket. Apply the
runtime tmpfiles rule only to the new `/run/buster-bench-systemd-broker`
directory; do not reapply the state/lease rules to a live queue. Do not enable
or start the socket until the operator has read back the binary and unit
identities. A socket start is separate from enabling GitHub dispatch.

The revised service binary changes the gateway executable digest. Update only
the fixed `gateway` sudo rule to its reviewed new digest; retain the same
three permitted operations and rerun positive and negative authorization
tests. The candidate and runner must have no access to the broker directory,
socket, system manager or queue/lease state.

## Required acceptance receipts

1. `systemd-analyze verify` the installed exact units and read back their
   hashes, ownership, effective properties, socket owner/mode and connection
   peer checks. Only `buster-bench` can connect. A malformed request, wrong
   peer, alternate stage/signal, path, revision or unit identity must fail
   without creating a unit or changing state.
2. Verify direct `StartTransientUnit` and privileged `systemctl kill` remain
   denied for service, candidate and runner identities. The broker must remain
   the sole manager write path for the service. Do not add `manage-units`, a
   unit-name-only polkit rule or a wildcard executable sudo rule.
3. With admission still disabled, exercise fixed outer and all five stage
   launches in an operator-controlled real-systemd rehearsal that proves
   stdout, stderr, status, timeout/cancellation, broker interruption, exact
   properties/UIDs, recursive absence and continuous lease identity. Any
   uncertain instance retains the quarantine lease and blocks new admission.
4. Retain the privileged host operator's signoff and the repository
   administrator's separate protected preflight before starting the first
   smoke job through the reviewed workflow. Five real attempts, bounded export,
   durable replay and a subsequent reservation remain the #880 acceptance work.

The broker code and reference units are a candidate for review. A local
self-test or installed socket alone is not a host qualification receipt.
