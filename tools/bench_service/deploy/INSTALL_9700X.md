# Install and verify the privileged Ryzen 7 9700X boundary

This procedure installs one reviewed service implementation on the dedicated
benchmark host. It is deliberately separate from GitHub dispatch and from live
recovery qualification. Installation never enables or starts the service and
never enables repository dispatch.

The installer is fixed-path and takes no arguments. It rejects symlinked or
mutable sources, unmanaged prior files, active benchmark units, a held lease,
metadata drift, identity drift and a changed lease inode. It does not repair a
drifting installation. The operator must investigate and review the drift.

## Preconditions

Use a trusted checkout of the reviewed commit on the 9700X host. The host must
provide systemd, polkit, sudo, Python 3, cgroup v2 and the repository's fixed
CPU-2 recipe configuration. Do not run the Actions runner as root or as the
`buster-bench` service user.

Before installation, stop and drain every `buster-bench*.service`. Confirm that
no benchmark workflow is queued or running. On a first installation, no
managed binary, unit, policy or identity file may already exist. On an update,
the currently installed verifier must pass before any file is changed.

Build and review these three artifacts from the same intended source revision:

- `buster-bench-service` — authenticated queue/service executable;
- `buster-bench-build` — fixed `validate-buster-v1` build driver;
- `buster-bench-throughput` — native throughput harness.

Stage only those artifacts under a root-owned, non-writable directory:

```sh
sudo install -d -o root -g root -m 0700 /opt/buster-bench/staging
sudo install -o root -g root -m 0555 PATH_TO_SERVICE \
  /opt/buster-bench/staging/buster-bench-service
sudo install -o root -g root -m 0555 PATH_TO_BUILD_DRIVER \
  /opt/buster-bench/staging/buster-bench-build
sudo install -o root -g root -m 0555 PATH_TO_THROUGHPUT \
  /opt/buster-bench/staging/buster-bench-throughput
```

The installer also consumes the reviewed unit, slice, sysusers, tmpfiles,
sudoers, polkit, verifier and recipe files from this checkout. Those files and
every staging artifact must be root-owned, single-link regular files with no
group or other write bit. The staged executables must have an owner execute
bit.

## Install

Run the installer directly through the system Python. It accepts no path,
identity, command, environment, recipe, profile or host argument:

```sh
sudo /usr/bin/python3 tools/bench_service/deploy/install_9700x.py
```

A successful installation prints `INSTALL_OK`, followed by the stable lease
device/inode, and confirms that the service remains stopped and dispatch
remains disabled.

The installer performs these operations while holding the stable lease:

1. installs the fixed users/groups and verifies service membership in the
   candidate group;
2. creates only missing fixed directories, then verifies exact ownership and
   modes rather than repairing existing drift;
3. creates `host.lock` once with `O_EXCL`, records its device/inode, and never
   unlinks or replaces it;
4. atomically publishes the reviewed binaries, recipe, unit, slice and
   authorization policy through same-directory temporary files plus `fsync`;
5. validates sudoers, reloads the systemd manager, and records the exact host,
   kernel, CPU topology, microcode, governor, scheduler and frequency identity;
6. publishes a closed SHA-256 manifest for every installed binary and policy;
7. runs the installed preflight verifier and rechecks the lease inode before
   releasing the install lock.

Any failed step leaves the service stopped. Do not manually overwrite one
managed file to continue; resolve the cause and rerun the reviewed installer.
Never remove or recreate `/var/lib/buster-bench/lease/host.lock` on an existing
installation.

## Authorize the runner identity

The runner receives no unit-management authority. Add only its dedicated OS
account to `buster-bench-dispatch`; do not add it to `buster-bench`,
`buster-bench-candidate` or `buster-bench-admin`:

```sh
sudo usermod -aG buster-bench-dispatch RUNNER_USER
```

Restart the runner login/session before testing authorization. The sudo policy
allows only the installed executable's typed `gateway` operations and always
changes to the unprivileged `buster-bench` principal. It grants no shell,
`systemctl`, `systemd-run`, SSH, path or environment selection.

The system-bus policy separately allows the service account to manage only the
deterministic per-job and per-stage unit names. The dispatch group is not
recognized by that policy. Long-lived service lifecycle control belongs to
root or the separately reviewed `buster-bench-admin` group.

## Preflight and live verification

Preflight verifies installed hashes, metadata, users/groups, host identity,
lease identity, unit definitions, slice limits, sudoers syntax and absence of
competing/stale benchmark units:

```sh
sudo /usr/local/libexec/buster-bench-verify preflight
```

Start, but do not enable, the service:

```sh
sudo systemctl start buster-bench.service
sudo /usr/local/libexec/buster-bench-verify live
```

The live verifier additionally binds the running process to its exact UID/GID,
boot ID, systemd invocation ID and cgroup ancestry. It opens every cgroup path
component without following links, verifies cgroup v2, singleton CPU 2,
effective memory/swap/PID limits across the ancestry, and requires the service
leaf to be populated. It atomically publishes bounded evidence at:

```text
/var/lib/buster-bench/verification/latest.txt
```

The evidence contains the installed-file digests, host snapshot, lease
identity, process identity, cgroup files and a final evidence SHA-256. Preserve
that file with the qualification record. It is not evidence for cancellation,
restart or reboot recovery; that belongs to the separate live-recovery PR.

## Activation gate

Keep GitHub dispatch disabled until all of these are true:

- this installer and live verifier passed on the registered host;
- the protected repository ruleset and `benchmark-9700x` environment are
  active;
- the fixed-gateway workflow-policy check is green on protected `main`;
- the service queue is idle and requires no reconciliation;
- no other workflow can target the benchmark labels; and
- an independent environment reviewer is configured.

Stop the service and disable dispatch immediately on any hash, topology,
microcode, scheduler, unit, authorization, cgroup, lease or host-identity drift.
