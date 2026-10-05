# Same-host service with MCP clients (#437)

The accepted #437 deployment is same-host: the queue daemon, the MCP adapter
and any tunnel client run on the benchmark machine itself. This is the
operator reference for upgrading an existing installation to a revision that
contains the native recipes and the MCP adapter, and for admitting personal
clients. It is not an installer and not a record that any host was changed;
record real installation facts on #437.

The off-host split in [OFFHOST_CONTROL.md](OFFHOST_CONTROL.md) is the upgrade
path if a second machine becomes available. Nothing here depends on it.

## What same-host means for clients

The reference unit runs the synchronous `serve` loop. While a job runs it does
not serve the public socket, so:

- status, result, log and artifact calls made during a job time out; they do
  not return cached state;
- a submission made during a job also times out and must be retried with the
  **same** idempotency key afterwards;
- an upload interrupted by a job resumes from its durable cursor.

When more clients are waiting than the daemon's small connection backlog
holds, further requests are refused at once as `busy` without being sent.

A timeout or `busy` is the designed quiet behaviour, not a fault. Clients should wait
rather than poll in a tight loop.

Two things run on the measured machine that an off-host deployment would
keep away from it, and both are accepted limitations:

- every open MCP session is one idle adapter process blocked on its input;
- a tunnel client is a persistent process with a network connection.

Keep both off the measurement CPU. The reference job CPU is 2; read its SMT
sibling from `/sys/devices/system/cpu/cpu2/topology/thread_siblings_list` and
exclude both with `CPUAffinity=` on the tunnel unit. Do not treat this as a
noise qualification: #422 and #426 own measured noise evidence.

## Before changing the host

1. Install only a merged `main` revision. Record its full commit.
2. Follow the host's existing single-operator protocol and hold the operator
   window for the whole change.
3. Confirm the service is idle: no active job, no stage unit, no reconciliation
   or quarantine pending. Do not upgrade over an unfinished job.
4. Stop the service and broker socket, then copy the queue directory
   `/var/lib/buster-bench/queue` to operator-owned storage and hash the copy.
   Never move, replace or copy over `/var/lib/buster-bench/lease/host.lock`.

## Binaries that must be replaced together

Build with the reviewed Clang as described in
[VALIDATE_BUSTER_V1.md](VALIDATE_BUSTER_V1.md) and [SYSTEMD_BROKER.md](SYSTEMD_BROKER.md),
from the one recorded revision:

| Installed path | Why it changes |
| --- | --- |
| `/usr/local/libexec/buster-bench-service` | New recipes, upload store, MCP adapter, native driver and sampler verbs. |
| the systemd broker binary | Admits the `native-execute` and `native-runtime` stages and their sandbox properties. |
| `/usr/local/libexec/buster-bench-credential-gate` | Admits stages 29 and 30 for the service binary's `native-payload` and `native-sampler` verbs only. |

An old gate or broker with a new service refuses every native job at the
entry gate; a new gate with an old service has no such verbs to run. Replace
all three, record each SHA-256, and re-pin every digest that the installed
sudo and unit policy names, as [SYSTEMD_BROKER.md](SYSTEMD_BROKER.md) requires
for any service refresh. The build driver and throughput harness are not
changed by the native recipes.

## Recipe profiles

Materialization compares each served recipe's installed profile byte for byte
with the copy compiled into the service. Install, from the same revision,
into `/opt/buster-bench/installed/recipes` with the existing owner and mode:

- `tools/bench_service/profiles/native-execute-v1.recipe`
- `tools/bench_service/profiles/native-runtime-v1.recipe`

alongside the profiles already there.

A store written by a revision that sealed bundles owner-only (mode 0500) is
not readable by the broker. Remove those bundles from `queue/native-blobs`
while the service is stopped and upload the programs again. Without them the capabilities reply
still lists both recipes and a submission is accepted, but the job fails at
materialization as a recipe mismatch.

## The journal upgrade is one-way

The new service reads the existing journal and writes new records at journal
generation 6. A service built before the native recipes rejects such a
journal as corrupt. Rollback is therefore the old binaries **and** the queue
copy taken above, and is only clean if no job was accepted in between. Decide
before accepting the first job.

## Verify before admitting clients

As the service account, through the existing pinned gateway rule:

```sh
sudo -n -u buster-bench -- /usr/local/libexec/buster-bench-service gateway capabilities
```

Require `journal=6`, `export=1`, and `service-recipes=` listing
`validate-buster-v1`, `zen5-calibration-v1`, `native-execute-v1` and
`native-runtime-v1`. Run one `validate-buster-v1` job through the existing
path first, so that a regression in the upgrade is not confused with a native
recipe problem.

## Admitting a personal client

The public socket accepts only a peer whose UID and GID equal the daemon's.
A personal client therefore needs one additional pinned sudo rule, for exactly
this command and no wildcard:

```text
/usr/local/libexec/buster-bench-service mcp /run/buster-bench/control.sock
```

run as `buster-bench`, pinned by digest like the existing `gateway` rule.
This is a deliberate widening of the trust boundary: whoever may run it can
submit, cancel and read every job, and can upload programs that execute under
the candidate account inside the existing containment. It grants no shell and
no other verb. The adapter reports the fixed `github-actions` principal; there
is no per-user identity.

### Claude Code and Codex

Both start a stdio MCP server as a local command, so no tunnel is needed.
Configure the server command as an SSH invocation of the pinned rule, for
example:

```sh
ssh <operator>@<host> sudo -n -u buster-bench -- \
    /usr/local/libexec/buster-bench-service mcp /run/buster-bench/control.sock
```

The SSH command must print nothing else on stdout: no banner, no shell
profile output. Close the session when finished.

### ChatGPT

ChatGPT needs the operator-installed Secure MCP Tunnel client described in
[MCP_CLIENT.md](MCP_CLIENT.md#private-chatgpt-connection), forwarding to the
same pinned command on this host. Run it as its own unit under a dedicated
account that holds only that sudo rule, with the CPU affinity above. Keep the
tunnel credential in operator-managed storage, never in a request, the
repository, a workspace or a log.

## Refreshing an installation

Every refresh follows the same order. It takes about a minute of downtime
when the service is idle. Keep a log of each step with its timestamp.

1. **Build first, change nothing.** In a fresh checkout of the merged
   revision, as root, with the service still running and idle:

   ```sh
   clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable \
       -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o build/buster-bench-build
   build/buster-bench-build bench_service capabilities
   build/buster-bench-build bench_service self-test
   build/buster-bench-build bench_service_broker self-test
   clang -Isrc -DBUSTER_SINGLE_THREADED=1 -std=c11 -O2 -Wall -Wextra -Werror \
       -fwrapv -fno-strict-aliasing -funsigned-char \
       tools/throughput/throughput.c tools/throughput/shared.c -lm -o build/throughput
   ```

   The build driver must show a non-executable `GNU_STACK`, and the two gates
   under `build/bench-service-tools/` must have no `INTERP` or `DYNAMIC`
   program header. Stop if a self-test fails. The self-tests use temporary
   directories and do not touch the installation.

2. **Confirm idle, then stop.** No `buster-bench-<job>-*` unit is active and
   nothing holds the lease (`grep ':<lease inode> ' /proc/locks` is empty).
   Then `systemctl stop buster-bench.service buster-bench-systemd-broker.socket`.

3. **Take the rollback copy** into a new operator-owned directory: every
   `/usr/local/libexec/buster-bench-*`, the `recipes` directory, the runner
   sudo rule and the whole queue directory. Hash it.

4. **Install** each binary with `install -o root -g root -m 0755` to a
   temporary name in the same directory, rename it into place and `cmp` it
   with the build output:

   | Build output | Installed name |
   | --- | --- |
   | `build/bench-service-tools/service` | `buster-bench-service` |
   | `build/bench-service-tools/systemd-broker` | `buster-bench-systemd-broker` |
   | `build/bench-service-tools/credential-gate` | `buster-bench-credential-gate` |
   | `build/bench-service-tools/broker-entry-gate` | `buster-bench-broker-entry-gate` |
   | `build/buster-bench-build` | `buster-bench-build` |
   | `build/throughput` | `buster-bench-throughput` |

   Compare the installed unit and tmpfiles definitions with
   `tools/bench_service/deploy/`; reinstall and `daemon-reload` only if they
   differ. Install every file under `tools/bench_service/profiles/` into the
   `recipes` directory as `root:buster-bench` mode 0440, restoring the
   directory to 0550 afterwards.

5. **Re-pin the runner sudo rule.** It names the service binary by SHA-256.
   Replace the old digest with the new one in a copy, check the copy with
   `visudo -c -f`, then rename it into place and run `visudo -c`. A stale
   digest silently breaks the Actions dispatch path.

6. **Start** the broker socket, then the service. Check `gateway
   capabilities`, and read back one old finished job with `gateway result` to
   confirm the journal replays.

7. **Prove the old path first.** Run one `validate-buster-v1` job on two
   revisions that are already installed before submitting anything new.

## Recovering a held attempt

A job that cannot start, or whose cleanup cannot be proven, is not guessed
away. It stays in `preparing` (or a later active phase) with a failure reason
such as `cleanup-failed` or `worker-mismatch`, `result-bound=0`, and the
service keeps the lease. Every later request is refused. This is deliberate,
and it needs an operator.

First find out why and fix that, or the next job will do the same. The
broker's `BQ-BROKER-DIAG-V1 REQUEST` journal line shows which of
`state_valid`, `signal_valid` and `command_valid` failed; no broker line at
all means the service refused before asking.

Then, for job `J` and attempt token `T`:

1. **Record, changing nothing.** The outer unit `buster-bench-J-T.service`
   and its stage units must be absent or failed, with no process and no
   cgroup left. Hash the journal, `attempt-J`, `failure-J`, `worker-J` and
   the files in the result directory. If any job process is still alive,
   stop here: this procedure does not prove cleanup.
2. **Stop** the service and the broker socket. Nothing may hold the lease
   afterwards, and the hashed files must be unchanged.
3. **Reconcile under the lease**, as the service account:

   ```sh
   sudo -u buster-bench -g buster-bench \
       flock -n -E 75 /var/lib/buster-bench/lease/host.lock \
       /usr/local/libexec/buster-bench-service workspace-reconcile \
       /var/lib/buster-bench/queue /var/lib/buster-bench/workspaces J T
   ```

   It must exit 0 and print the job as `phase=finished outcome=interrupted`.
   Only the journal may have changed; the failure record and the unbound
   result files stay as evidence.
4. **Reset** each failed `buster-bench-*` unit after saving its journal.
5. **Start** the socket and the service, read the job back, and confirm the
   lease is unheld.

## Program store maintenance

`queue/native-blobs` holds at most 128 entries for its lifetime, counting
unfinished uploads (`<digest>.part`) and interrupted publications
(`.<digest>`). Nothing removes entries automatically. With the service
stopped, an operator may remove whole bundle directories and stale partial
files; a removed program is simply uploaded again. Never remove an entry
that a queued job names.

A bundle sealed by a revision older than the group-readable store (owner-only
mode 0500) cannot be read by the broker and must be removed and uploaded
again.

## Repeating the quiet-phase check

After a refresh that touches the transport or the worker, repeat the check
that #437 accepts. Run the load from a unit pinned away from the job CPU.

1. Submit one `validate-buster-v1` job and wait until its outer unit is
   active. Record the outer unit's main PID and invocation, the lease holder,
   the service unit's `CPUUsageNSec` and the journal size.
2. While it runs, issue 100 submit attempts and 1,000 status or log requests
   as the service account, many at a time. Use the running job's own key for
   most submits, so that a retry that reaches the backlog creates nothing,
   and a few distinct cheap native submissions.
3. Record the same facts again while the job is still active.
4. After the job finishes, resubmit the distinct submissions with their
   original keys and wait for them.

It passes when every request during the job failed as `busy` or
`io-uncertain` within the client bound, the recorded PID, invocation and
lease holder are identical, the journal did not grow during the burst, the
service's CPU time moved by no more than a few milliseconds, the job
succeeded, and the only new jobs are the distinct submissions, finished
after the first job's manifest was written.

## Acceptance receipts for #437

Record on the issue, with the installed revision and the three digests:

1. From ChatGPT: a real `bench_submit`, its durable job ID, and after
   disconnecting, that same job's `bench_result`. Repeat from Codex or Claude
   Code. Tool enumeration alone does not count.
2. One native program, for example an assembly microkernel assembled
   elsewhere: `bench_program_begin`/`write`/`finish`, a `native-execute-v1`
   job and a `native-runtime-v1` job on the same manifest digest, and the
   archive fetched with `bench_artifact_receipt` and `bench_artifact_read`
   whose SHA-256 equals the receipt's `archive_sha256`.
3. The amended quiet-phase test, as described under
   [Repeating the quiet-phase check](#repeating-the-quiet-phase-check).
4. The effective containment of a native stage unit as the manager reports
   it, and an empty process tree after the job.

A successful execution or a set of runtime samples is not a performance
result and qualifies nothing about the host's noise.
