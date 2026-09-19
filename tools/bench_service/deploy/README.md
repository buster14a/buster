# Dedicated-host deployment boundary

This directory contains the reviewed systemd, identity, filesystem and
authorization policy for the dedicated benchmark host, plus a fixed-path
installer and a live verifier. Nothing here is applied by `build.c`. The
installer takes no arguments, must be invoked explicitly by a privileged
operator, leaves the service stopped, and leaves GitHub dispatch disabled.

Use these documents in order:

1. [validate-buster-v1 provisioning and evidence checklist](VALIDATE_BUSTER_V1.md)
   for the repository/service prerequisites;
2. [9700X installation and verification](INSTALL_9700X.md) for the privileged
   host boundary; and
3. the separate protected-dispatch and live-recovery procedures before any
   performance workflow is trusted.

The installed executable is the reviewed `bench_service` binary at
`/usr/local/libexec/buster-bench-service`. The queue, workspace root and stable
lease inode live under `/var/lib/buster-bench`; the frozen recipe/source store
lives at `/opt/buster-bench/installed`. The lease path is
`/var/lib/buster-bench/lease/host.lock`, with a root-owned parent that prevents
the service, candidate and runner identities from replacing it. The installer
creates the lease once with `O_EXCL`, records its device/inode and refuses an
active or drifting lease. Never move, unlink or recreate it on an existing
installation.

State is provisioned under dedicated identities. Installed executables and
policy are root-owned and immutable to service, candidate and runner users.
Paths must not be symlink aliases. The service account belongs to the
`buster-bench-candidate` group only for read/execute traversal of
candidate-owned staging output. Candidate build and throughput services use
the candidate account as both explicit UID and primary GID and never receive
the queue or lease group.

The fixed recipe handoff expects the reviewed build driver at
`/usr/local/libexec/buster-bench-build` and native throughput harness at
`/usr/local/libexec/buster-bench-throughput`. The service supplies only the six
recipe identities documented in the service README; those paths and every
build/throughput option remain build-policy constants. The installed verifier
checks a closed SHA-256 manifest covering those binaries, the service binary,
recipe, unit, slice, sysusers, tmpfiles, sudoers, polkit, host identity and
stable-lease identity.

The example CPU (`2`) and budgets are host policy, not portable defaults. CPU 2
is also compiled into `build.c::BENCH_SERVICE_RECIPE_CPU`; changing only the
service argument or slice is insufficient. The verifier requires matching
installed driver, service argument, slice identity and effective cgroup-v2
limits before admission.

Each transient service repeats the reviewed sandbox: the queue and host lease
are inaccessible, the installed tree is read-only, the private workspace is
the only writable tree, and `ProtectSystem=strict`, `PrivateTmp`,
`PrivateDevices`, `NoNewPrivileges`, `ProtectHome`, `ProtectControlGroups`,
`ProtectKernel*`, `ProtectProc=invisible`, `RestrictNamespaces`,
`RestrictSUIDSGID`, `RestrictAddressFamilies=AF_UNIX` and the fixed
system-service syscall filter are enabled. The build driver locks
baseline/candidate build trees before throughput and retains result bundle plus
failure/cancellation outcome evidence for retrieval.

Recipe stages use deterministic sibling service names and bind their lifetime
to the outer worker with `PartOf=`, `BindsTo=` and `After=`. The coordinator
does not rely on dependency propagation alone: while retaining the host lease,
it explicitly cleans and proves absence of every stage unit and cgroup before
another measurement may start. The installed polkit rule grants the service
account only the fixed per-job/per-stage unit namespace. The runner dispatch
group has no systemd authority and can invoke only typed fixed-gateway commands
through sudo as the service principal.

The candidate handoff uses a separate `candidate/staging` directory with the
candidate account as its only non-owner authority. The candidate service may
read the group-readable materialized sources and baseline build and may write
only that staging path. The final candidate build is trusted-owned,
group-readable/executable and group-nonwritable. The trusted service alone
promotes and locks the final binary and copies the complete throughput tree
into unpredictable trusted staging before no-replace directory publication.

The long-lived service needs no Linux capability and empties both capability
sets. It retains only local `AF_UNIX` access for the system manager, read-only
cgroup/proc inspection and its private state paths. `PrivateUsers` and
`ProcSubset=pid` are intentionally absent: a private user namespace can change
peer credentials used by system-bus policy, while hiding non-process proc
entries would hide the kernel boot ID.

A successful install and `verify_9700x.py live` run qualify only the installed
identity and cgroup boundary. They do not prove cancellation, worker restart,
reboot, failed-cleanup recovery, subsequent admission, result export or
performance validity. Those claims require their separate PRs and retained
live evidence. No request field is executable.
