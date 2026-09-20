# Deployment references, not host qualification

These files document the paths and resource values expected by the repository
supervisor. They are not an installer, are never applied by `build.c`, and do
not qualify a benchmark host. Installation and any system-bus authorization
remain explicit operator work.

Use the [validate-buster-v1 provisioning and evidence checklist](VALIDATE_BUSTER_V1.md)
before starting the reference service. It separates repository prerequisites,
privileged operator work, and still-unperformed live checks. The reference now
selects the authenticated long-lived `serve` endpoint, not the one-shot
`worker-run` CLI. The executable includes the fixed gateway and
[bounded result exporter](../EXPORT.md); granting the gateway access remains
operator work. No deployment or live host qualification is implied.

The installed executable must be the reviewed `bench_service` binary at
`/usr/local/libexec/buster-bench-service`. The queue, workspace root and stable
lease inode live under `/var/lib/buster-bench`; the frozen recipe/source store
lives at `/opt/buster-bench/installed`. The new-installation lease path is
`/var/lib/buster-bench/lease/host.lock`, with a private `lease` parent. The state
parent is group-traversable and the workspace root is SGID to the candidate
group; the queue, lease parent and durable results remain service-private.
Never move or replace an existing lease as part of applying this reference.
State is provisioned once under the dedicated account; installed executables
must be operator-owned and immutable to service/candidate/runner users. Paths
must not be symlink aliases. Provision the dedicated
`buster-bench-candidate` user and group as well. The service account is a member
of that group only for read access to candidate-owned staging output; candidate
build and throughput services use the candidate account as both their explicit
UID and primary GID and never receive the service account's queue or lease
group.

The fixed recipe handoff additionally expects the reviewed build driver at
`/usr/local/libexec/buster-bench-build` and the native throughput harness at
`/usr/local/libexec/buster-bench-throughput`. The service supplies only the
six recipe identities documented in the service README; those two executable
paths and every build/throughput option remain build-policy constants.

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

Do not enable the service yet. This change supplies the fixed recipe boundary
but does not install dependencies, run systemd, qualify a host or claim a
successful performance measurement. Live-systemd qualification and narrowly
service authorization remain explicit operator work; no request field is
executable.

The reference service needs no Linux capability and explicitly empties both
capability sets. It retains only local `AF_UNIX` access for the system manager,
read-only cgroup/proc inspection, and its private state paths. `PrivateUsers`
and `ProcSubset=pid` are intentionally absent: a private user namespace can
change peer credentials used by system-bus policy, while hiding non-process
proc entries would hide the kernel boot ID. Those residual exposures and the
fixed system-bus authorization must be reviewed during live qualification.
