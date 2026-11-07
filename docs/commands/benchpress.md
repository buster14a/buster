# benchpress

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock. [Command index](README.md).

`benchpress` is the Ryzen 7 9700X: every timing and every native measurement the
repository records comes from it, under its lock. Agent sessions reach it on the
owner's tailnet; a cloud session doesn't need a CI job or the owner to measure.

## Reaching it

- **Joining:** `.claude/hooks/join-tailnet.sh`, the SessionStart hook of Claude
  Code's and Codex's sessions, joins the tailnet with the environment's
  `TS_AUTHKEY`, as a `tag:llm` node named `claude-cloud-…`, in userspace
  networking (`tailscaled --tun=userspace-networking --state=mem:
  --socks5-server=localhost:1055`, the default socket). It replaces a daemon
  that isn't up, and one that is up but can't reach benchpress while the control
  server lists benchpress online (a stale daemon: `tailscale status` lists peers
  that no ping reaches). A session whose checkout predates the hook, or whose
  hook ran before the daemon went stale, runs it again by hand:
  `bash .claude/hooks/join-tailnet.sh`.
- **One node per container:** the daemon keeps its node key in memory, and the
  hook removes a `/var/lib/tailscale/tailscaled.state` an earlier hook left. A
  key on disk outlives the container in its saved filesystem, and #653's session
  saw its node used from elsewhere, most likely by a container started from such
  a copy: the IP stayed the same while the hostname changed every few seconds to
  names the session never chose (`grep 'active login'
  /var/lib/tailscale/log`), and pings to benchpress failed until the state file
  was moved aside. A session that sees this runs the hook again by hand after
  stopping its daemon (`pkill -x tailscaled`).
- **Checking:** `tailscale status` lists `benchpress`;
  `tailscale ping -c 2 --until-direct=false benchpress` must answer `pong`
  (through a DERP relay: the cloud egress has no UDP, so no direct connection is
  made, and plain `tailscale ping` exits 1 for that alone). A ping of every peer
  timing out means this node's daemon is stale, not benchpress: run the hook.
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
