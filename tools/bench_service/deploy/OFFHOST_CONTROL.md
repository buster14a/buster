# Off-host control and worker installation (#437)

This is the operator reference for the split deployment: a **control host**
owns the durable FIFO journal, the public client socket and the sealed result
cache; the benchmark **worker** owns execution, the physical lease and
cleanup. It documents what the source at this revision requires. It is not a
record that any host has been installed, and local fixtures do not qualify a
deployment. Record real installation facts on #437.

The control host must be a different machine from the worker. A same-host
installation exercises the code but cannot show that client traffic stays off
the measured machine.

## Fixed names

The binary compiles these in; no request, flag or environment variable
changes them.

| Item | Value | Used by |
| --- | --- | --- |
| Service binary | `/usr/local/libexec/buster-bench-service` | both hosts |
| Control journal root | operator argument to `control-serve` (`/var/lib/buster-bench-control/queue` is the reference) | control |
| Public client socket | operator argument to `control-serve` | control |
| Worker session socket | `/run/buster-bench/worker.sock` | control |
| Worker machine identity | `/etc/buster-bench/worker-id.sha256` | control |
| SSH client configuration | `/etc/buster-bench/worker-ssh.conf` | worker |
| SSH host alias | `buster-control` | worker |
| Worker queue root | `/var/lib/buster-bench/queue` | worker |
| Installed sources and tools | `/opt/buster-bench/installed` | worker |
| Workspaces | `/var/lib/buster-bench/workspaces` | worker |
| Host lease | `/var/lib/buster-bench/lease/host.lock` | worker |

## Control host

Run one daemon as the dedicated service account:

```sh
/usr/local/libexec/buster-bench-service control-serve \
    /var/lib/buster-bench-control/queue \
    /run/buster-bench/control.sock \
    /run/buster-bench/worker.sock
```

- The third argument must be `/run/buster-bench/worker.sock`: the SSH bridge
  connects to that compiled path, not to an argument.
- The two socket paths must differ. Both sockets accept only a peer whose
  effective UID and GID equal the daemon's (`SO_PEERCRED`). Clients therefore
  run as that account, normally through a narrow `sudo -u` rule for the
  `gateway`, `client` and `mcp` verbs.
- `/etc/buster-bench/worker-id.sha256` holds exactly 64 lowercase hex
  characters and no newline. It must be a regular single-link file owned by
  root and not group- or world-writable. The daemon refuses to start
  otherwise. The value names the one worker this control instance assigns to;
  choose it once and keep it.
- The journal root is a private directory on a local filesystem. The daemon
  is its only writer. On first start with an empty journal it records a random
  control-instance identity; a journal that already has jobs but no instance
  record is refused as a configuration mismatch rather than adopted.
- The daemon never opens a connection to the worker and never executes work.
  While an assignment is quiet it answers status, result and log requests
  from its own journal and cache.

The stdio MCP adapter runs on this host against the public socket:

```sh
/usr/local/libexec/buster-bench-service mcp /run/buster-bench/control.sock
```

See [MCP_CLIENT.md](MCP_CLIENT.md) for client configuration.

### SSH account for the worker

The worker reaches the control host as one dedicated account whose only
permitted command is the bridge. That account must have the same UID and GID
as the control daemon, because the bridge connects to the worker session
socket and is authenticated by peer credentials.

In that account's `authorized_keys`, pin the worker's key to the bridge and
remove every forwarding and terminal facility:

```text
restrict,command="/usr/local/libexec/buster-bench-service worker-stdio" ssh-ed25519 AAAA... buster-worker
```

`worker-stdio` relays bounded, validated session frames between the SSH
pipes and `/run/buster-bench/worker.sock`. It has no inactivity timer and
sends nothing on its own. Do not configure `ClientAliveInterval` for this
account: a server-side keepalive is traffic to the measured host during the
quiet phase.

## Worker host

The existing broker, credential gate, accounts and systemd units from
[SYSTEMD_BROKER.md](SYSTEMD_BROKER.md) stay as they are. The off-host agent
replaces the local `serve` loop as the thing that admits work:

```sh
/usr/local/libexec/buster-bench-service worker-agent
```

It takes no arguments. It owns local custody and the lease, and it starts
`/usr/bin/ssh` itself with keepalives, TCP keepalive and rekey-by-time
disabled and a ten-second connect timeout. It does not reconnect or send
ordinary messages while a job executes.

`/etc/buster-bench/worker-ssh.conf` must be a regular single-link file owned
by root, in a root-owned directory, with neither writable by group or others.
The agent refuses to open a session otherwise. It must define the alias
`buster-control`. A minimal configuration pins every identity explicitly:

```text
Host buster-control
    HostName <control host address>
    User <control service account>
    IdentityFile /etc/buster-bench/worker-ssh.key
    IdentitiesOnly yes
    UserKnownHostsFile /etc/buster-bench/control-known-hosts
    StrictHostKeyChecking yes
    ControlMaster no
    ForwardAgent no
```

Populate the known-hosts file from the control host's verified host key.
Never accept a key on first use.

## What the split guarantees, and what it does not

- Submissions are persisted on the control host before they are
  acknowledged. Losing the SSH session never releases or redispatches an
  assignment; elapsed time is never treated as ownership.
- Cancellation is recorded as durable intent on the control host and is the
  one message the control host sends to a quiet worker. `cancel_requested`
  is distinct from the worker's applied outcome.
- Results reach the cache as the worker's original sealed bytes and are
  verified against the receipt before the terminal acknowledgement.
- If every custodian of the lease is lost, including across a reboot, the
  worker records a lease-gap quarantine. The software does not claim that an
  open file description survived.
- Native program bundles are transferred to the worker through the same
  session for `native-execute-v1` and `native-runtime-v1`.
- The capabilities reply reports `executor=off-host-control-unqualified` and
  `qualification=local-fixtures-only`. Those tokens stay until an installed
  deployment has been witnessed and recorded.

## Acceptance still required on real hosts

Fixtures cover the protocol, custody, restart and cache paths with separate
control and worker roots on one machine. They do not cover, and this document
does not claim:

1. the actual accounts, UID/GID equality and file modes on both hosts;
2. sshd behaviour during a quiet job, including that no keepalive or rekey
   traffic reaches the worker;
3. systemd stop and restart policy for both daemons, and recovery after a
   real reboot of either host;
4. a burst of client submissions and cached reads during a real quiet phase
   producing no worker-side activity;
5. write-capable ChatGPT and Codex clients submitting through the adapter
   and later retrieving that job's result.
