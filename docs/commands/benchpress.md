# benchpress

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock. [Command index](README.md).

`benchpress` is the Ryzen 7 9700X: every timing and every native measurement the
repository records comes from it, under its lock. Agent sessions reach it on the
owner's tailnet; a cloud session doesn't need a CI job or the owner to measure.

## Reaching it

- **Joining:** `.claude/hooks/join-tailnet.sh`, the SessionStart hook of Claude
  Code's and Codex's sessions, joins the tailnet with the environment's
  `TS_AUTHKEY`, as an ephemeral `tag:llm` node named `claude-cloud-…`, in
  userspace networking (`tailscaled --tun=userspace-networking
  --socks5-server=localhost:1055 --socket=/var/run/tailscale/tailscaled.sock
  --state=mem:`). It replaces a daemon on the default socket that isn't up, one
  that keeps its state on disk, and one that is up but can't reach benchpress
  while the control server lists benchpress online (a stale daemon: `tailscale
  status` lists peers that no ping reaches). A session whose checkout predates
  the hook, or whose hook ran before the daemon went stale, runs it again by
  hand: `bash .claude/hooks/join-tailnet.sh`. A session that starts in the
  folder holding the checkout runs it through `.claude/hooks/parent-folder.sh`
  ([llm-memory](llm-memory.md#sessions-that-start-beside-the-checkout)).
- **Only in a disposable session:** the hook manages the daemon only where the
  session is positively identified as disposable: Claude Code's cloud
  (`CLAUDE_CODE_REMOTE=true`), or an environment that sets
  `BUSTER_TAILNET_DISPOSABLE=1`, as Codex's cloud environment must to join.
  Neither process 1's name nor a container says that a host's daemon is
  disposable. On any other machine it leaves the daemon and its state alone. A
  daemon on another socket is never touched; one whose `--socket` names the
  default socket, in any of the flag's forms or through `/var/run`'s link to
  `/run`, is on the default socket. `.claude/hooks/test-join-tailnet.sh` (the
  `DCO hook regression` job) checks which daemons it stops and that no state
  file changes, with stand-ins for the daemon and its tools.
- **One identity per session:** the environment's image holds the state file of
  a daemon an earlier hook started in `/var/lib/tailscale`. A daemon that reads
  it registers with that file's machine key, the same in every session, so the
  control server merges all those sessions into one node, and each
  registration replaces the others' keys. A node in that state answers no
  ping, and its own name in `tailscale status` changes every minute or so. The
  hook's in-memory state gives each daemon a machine key of its own, without
  reading or removing the file. Don't start `tailscaled` by hand without
  `--state=mem:`.
- **Checking:** `tailscale status` lists `benchpress`;
  `tailscale ping -c 2 --until-direct=false benchpress` must answer `pong`
  (through a DERP relay: the cloud egress has no UDP, so no direct connection is
  made, and plain `tailscale ping` exits 1 for that alone). A ping of every peer
  timing out means this node's daemon is stale or shares its identity, not that
  benchpress is down: run the hook. Before saying that benchpress can't be
  reached, run the hook and these checks, and report what they printed.
- **Connecting:** `tailscale ssh david@benchpress 'COMMAND'`. The
  `tailscale ssh` wrapper takes no `-o` options. `hostname` isn't installed
  there; use `uname -n`.

## Working there

- Read `~/buster-fast-bench/README-9700x.md` on benchpress: the lock, its holder
  file and the CI hooks.
- Run every build, test, benchmark and perf run through
  `EXCLUSIVE_OWNER=<name> EXCLUSIVE_TITLE=<title> EXCLUSIVE_LINK=<link> ~/buster-fast-bench/exclusive.sh COMMAND...`,
  per run, never around a series: a CI job waits at most 10 minutes for it.
  `cat /var/lib/buster-9700x/owner` shows who holds it.
- Keep a task's trees and results in `~/buster-fast-bench/<issue>-<agent>/`
  (`1282-claude`, `615-codex`): a clone of the repository at the commit it
  measures, its scripts and its results. Don't build in another session's
  directory.
- Start a run longer than a tool call's timeout detached, and poll it:
  `tailscale ssh david@benchpress 'cd DIR && (setsid nohup ./run.sh > run.log 2>&1 < /dev/null &)'`.
  Copy a script there through standard input
  (`tailscale ssh david@benchpress 'cat > DIR/run.sh' < run.sh`), and results
  back the same way (`tailscale ssh david@benchpress 'cat DIR/results.tsv'`).
- Pin a measurement to one CPU (`taskset -c 2`, as the 9700X workflow's
  benchmarks are).
- `pgrep -f` and `pkill -f` patterns match your own `ssh ... 'bash -c ...'`
  command line: use `pgrep -x NAME`, or a pattern with a letter in brackets
  (`[R]unner.Worker`).
- The raw results go to
  [buster-performance](https://github.com/buster14a/buster-performance/tree/main/docs/measurements),
  never to this repository (AGENTS.md's "Issue ownership").
