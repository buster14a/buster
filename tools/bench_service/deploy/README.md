# Deployment references, not host qualification

These files document the paths and resource values expected by the repository
supervisor. They are not an installer, are never applied by `build.c`, and do
not qualify a benchmark host. Installation and any system-bus authorization
remain explicit operator work.

The installed executable must be the reviewed `bench_service` binary at
`/usr/local/libexec/buster-bench-service`. The queue, workspace root and stable
lease inode live under `/var/lib/buster-bench`; the frozen recipe/source store
lives at `/opt/buster-bench/installed`. All are provisioned once, owned by the
dedicated account, and must not be symlink aliases.

The example CPU (`2`) and budgets are review fixtures, not portable defaults.
Change the service argument and slice together. The worker also repeats the
values on each transient scope and fails closed unless manager properties and
cgroup-v2 files show the exact leaf limits and non-tighter ancestor limits.

Do not enable the service yet. This PR deliberately has no installed recipe
executor and therefore cannot produce a successful measurement. A later PR
must add that fixed entrypoint, live-systemd qualification, and narrowly scoped
authorization without making any request field executable.

The reference service needs no Linux capability and explicitly empties both
capability sets. It retains only local `AF_UNIX` access for the system manager,
read-only cgroup/proc inspection, and its private state paths. `PrivateUsers`
and `ProcSubset=pid` are intentionally absent: a private user namespace can
change peer credentials used by system-bus policy, while hiding non-process
proc entries would hide the kernel boot ID. Those residual exposures and the
fixed system-bus authorization must be reviewed during live qualification.
