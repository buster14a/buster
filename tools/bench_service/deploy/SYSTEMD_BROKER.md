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

## Build and review

From the reviewed checkout, run `./build.sh bench_service_broker self-test` and
`./build.sh bench_service self-test`. The former constructs all six fixed
launches and exercises malformed request rejection without manager access.
Review the broker source, this packet, the socket/template service, tmpfiles,
and the changed service/build-driver call sites as one transition. Build the
installed service, build driver, throughput tool and broker from one selected
protected-main commit. Record SHA-256 of all binaries, dependencies, units and
the complete immutable source inventory. The broker must be root-owned,
executable and unwritable by service, candidate and runner accounts.

## Host installation boundary

Keep dispatch false, the long-lived service stopped and all prior outer/stage
units and cgroups absent while installing. Preserve the existing lease file;
never recreate, truncate, rename or replace it. After a fresh no-follow `stat`
of the single-link service-owned `0600` lease, install a root-owned `0444`
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
`buster-bench:buster-bench` `0600`. The template process runs as root with empty
capability sets, no new privileges, a read-only filesystem view and only local
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
