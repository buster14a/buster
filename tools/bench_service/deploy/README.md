# Deployment references, not host qualification

These files document the paths and resource values expected by the repository
supervisor. They are not an installer, are never applied by `build.c`, and do
not qualify a benchmark host. Installation and any system-bus authorization
remain explicit operator work.

Use the [validate-buster-v1 provisioning and evidence checklist](VALIDATE_BUSTER_V1.md)
before starting the reference service. It separates repository prerequisites,
privileged operator work, and still-unperformed live checks. The reference now
selects the authenticated long-lived `serve` endpoint, not the one-shot
`worker-run` CLI. The installed service includes the fixed gateway and
[bounded result exporter](../EXPORT.md); this directory still supplies no
runner authorization, durable publication destination or physical-host
qualification. The constrained broker design and operator checks are in
[SYSTEMD_BROKER.md](SYSTEMD_BROKER.md).

The installed executable must be the reviewed `bench_service` binary at
`/usr/local/libexec/buster-bench-service`. The queue, workspace root and stable
lease inode live under `/var/lib/buster-bench`; the frozen recipe/source store
lives at `/opt/buster-bench/installed`. The new-installation lease path is
`/var/lib/buster-bench/lease/host.lock`, with a private `lease` parent. The state
parent is group-traversable and the workspace root is SGID to the candidate
group; the queue and lease parents allow traversal by the service group only,
while their files and durable result leaves remain service-private.
Never move or replace an existing lease as part of applying this reference.
State is provisioned once under the dedicated account; installed executables
must be operator-owned and immutable to service/candidate/runner users. Paths
must not be symlink aliases. Provision the dedicated
`buster-bench-candidate` user and group as well. The service account is a member
of that group only for read access to candidate-owned staging output; candidate
build and throughput services use the candidate account as both their explicit
UID and primary GID and never receive the service account's queue or lease
group.

The broker instance keeps UID 0 with empty capability sets, takes the service
group as its primary group and the candidate group as a supplementary group.
The fixed queue and lease parents are `0710` service:service; the stable lease
is `0640`, and broker-readable worker records are `0440`. The results parent
is `0710` service:service, but each result leaf remains `0700`. No candidate or
runner identity belongs to the service group. The broker traverses the
candidate-group workspace ancestry and checks exact owner, group and mode
before reading the fixed manifests; it cannot list the private result leaf.

The fixed recipe handoff additionally expects the reviewed build driver at
`/usr/local/libexec/buster-bench-build` and the native throughput harness at
`/usr/local/libexec/buster-bench-throughput`. The service supplies only the
six recipe identities documented in the service README; those two executable
paths and every build/throughput option remain build-policy constants.
The installed build driver must have a nonexecutable ELF stack (a `GNU_STACK`
program header with `RW`, without `E`). The reference transient sandbox keeps
`MemoryDenyWriteExecute=yes`; a TCC bootstrap executable without that header
cannot spawn recipe stages there. Build and hash a reviewed Clang driver from
the same `build.c` source for this installed path. The ordinary `build.sh`
bootstrap remains TCC based.

The example CPU (`2`) and budgets are review fixtures, not portable defaults.
CPU 2 is also compiled into `build.c::BENCH_SERVICE_RECIPE_CPU`; changing only
the service argument and slice is insufficient. Require matching installed
driver, service argument and slice identities before enabling the service.
The worker also repeats the
values on each transient service and fails closed unless manager properties and
cgroup-v2 files show the exact leaf limits and non-tighter ancestor limits.

Each transient service also repeats the reference sandbox: the queue directory
and host lease are inaccessible, the installed tree is read-only, the private
workspace is the only writable tree, and `ProtectSystem=strict`,
`PrivateTmp`, `PrivateDevices`, `NoNewPrivileges`, `ProtectHome`,
`ProtectControlGroups`, `ProtectKernel*`, `ProtectProc=invisible`,
`RestrictNamespaces`, `RestrictSUIDSGID`, `RestrictAddressFamilies=AF_UNIX`
and the fixed system-service syscall filter are enabled. The build driver
locks baseline/candidate build trees before throughput and retains result
bundle plus failure/cancellation outcome evidence for retrieval.
The worker compares the manager's expanded `SystemCallFilter` with the
reviewed long-lived service's effective filter and accepts the manager's
numeric `SystemCallErrorNumber=1` spelling for `EPERM`. An active unit with
`Result=success` is still running until its active state and cgroup prove exit.
After exit, systemd may clear `ControlGroup` or collect the unit entirely.
The worker then requires the recorded invocation where available and proof
that the original cgroup leaf is absent. For a collected unit, it uses the
bounded `systemd-run --wait` process status for the outcome before finalizing.

Recipe stages use deterministic sibling service names and bind their lifetime
to the outer worker with `PartOf=`, `BindsTo=` and `After=` and are collected
when inactive or failed. The coordinator
does not rely on dependency propagation alone: while retaining the host lease,
it explicitly cleans and proves absence of every stage unit and cgroup before
another measurement may start.

The candidate build handoff uses a separate `candidate/staging` directory with
the candidate account as its only non-owner authority. The candidate transient
service is allowed to read the group-readable materialized sources and baseline
build, and to write only that staging path (including its throughput-results
subdirectory). The final `candidate/build` tree is trusted-owned,
group-readable/executable and group-nonwritable; queue, lease, result
directory, and installed policy tree remain inaccessible or service-private.
The trusted service UID alone promotes and locks the final candidate binary
and copies the complete throughput tree into an unpredictable trusted staging
directory before a no-replace directory publication. The workspace parent is
SGID to the candidate group so materialized source/build descendants inherit
the intended group without a request-controlled chown or path.

Do not enable the service yet. The broker socket, binary, root-owned lease
identity and service dependencies must be installed and independently reviewed
as one exact set. These references do not qualify a host or claim a successful
performance measurement. No request field is executable.

The reference service needs no Linux capability and explicitly empties both
capability sets. It retains only local `AF_UNIX` access for the system manager,
read-only cgroup/proc inspection, and its private state paths. `PrivateUsers`
and `ProcSubset=pid` are intentionally absent: a private user namespace can
change peer credentials used by system-bus policy, while hiding non-process
proc entries would hide the kernel boot ID. Those residual exposures and the
fixed system-bus authorization must be reviewed during live qualification.
