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

A timeout is the designed quiet behaviour, not a fault. Clients should wait
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
3. The amended quiet-phase test: while a job runs, 100 submit attempts and
   1,000 status/log requests time out, the job's process tree and lease are
   unchanged, and queued work starts only after cleanup.
4. The effective containment of a native stage unit as the manager reports
   it, and an empty process tree after the job.

A successful execution or a set of runtime samples is not a performance
result and qualifies nothing about the host's noise.
