# Deployment references, not host qualification

These files document the paths and resource values expected by the repository
supervisor. They are not an installer, are never applied by `build.c`, and do
not qualify a benchmark host. Installation and any system-bus authorization
remain explicit operator work.

The installed executable must be the reviewed `bench_service` binary at
`/usr/local/libexec/buster-bench-service`. The queue, workspace root and stable
lease inode live under `/var/lib/buster-bench`; the frozen recipe/source store
lives at `/opt/buster-bench/installed`. All are provisioned once, owned by the
dedicated account, and must not be symlink aliases. Provision the dedicated
`buster-bench-candidate` user and group as well. The service account is a member
of that group only for access to the candidate-owned staging tree; candidate
build services use the candidate account as both their explicit UID and primary
GID and never receive the service account's queue or lease group.

The fixed recipe handoff additionally expects the reviewed build driver at
`/usr/local/libexec/buster-bench-build` and the protected-main native-retirement
harness at `/usr/local/libexec/buster-native-retirement-performance`. The latter
is installed verbatim from `tools/bench_service/native_retirement_performance.py`;
the build driver rejects any other SHA-256. Provision the immutable experiment
definition at `/usr/local/share/buster-bench/native-retirement-performance-v1`
and compile its exact `definition.json` SHA-256 as
`BENCH_SERVICE_RECIPE_DEFINITION_SHA256`. The definition manifest recursively
binds every frozen input and tool used by the experiment. The harness returns
success only after the bound evidence validator and independent replay pass.
The service supplies only the six recipe identities documented in the service
README; both executable paths and every build/measurement option remain
build-policy constants.

The example CPU (`2`) and budgets are review fixtures, not portable defaults.
Change the service argument and slice together. The worker also repeats the
values on each transient service and fails closed unless manager properties and
cgroup-v2 files show the exact leaf limits and non-tighter ancestor limits.

Each transient service also repeats the reference sandbox: the queue directory
and host lease are inaccessible, the installed tree is read-only, the private
workspace is the only writable tree, and `ProtectSystem=strict`,
`PrivateTmp`, `PrivateDevices`, `NoNewPrivileges`, `ProtectHome`,
`ProtectControlGroups`, `ProtectKernel*`, `ProtectProc=invisible`,
`RestrictNamespaces`, `RestrictSUIDSGID`, `RestrictAddressFamilies=AF_UNIX`
and the fixed system-service syscall filter are enabled. The build driver
locks baseline/candidate build trees before measurement and retains result
bundle plus failure/cancellation outcome evidence for retrieval.

The candidate build handoff uses a separate `candidate/staging` directory with
the candidate account as its only non-owner authority. The candidate transient
service is allowed to read the group-readable materialized sources and baseline
build. The trusted measurement harness alone writes the staging
`throughput-results` subdirectory. The final `candidate/build` tree is trusted-owned,
group-readable/executable and group-nonwritable; queue, lease, result
directory, and installed policy tree remain inaccessible or service-private.
The trusted service UID alone promotes and locks the final candidate binary
and copies the complete performance-evidence tree into an unpredictable trusted staging
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
