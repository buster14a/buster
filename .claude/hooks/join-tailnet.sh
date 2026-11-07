#!/bin/bash
# SessionStart hook: joins the tailnet so that sessions reach benchpress (the 9700X) with
# `tailscale ssh david@benchpress` without asking (docs/commands/benchpress.md). It uses tailscaled's default socket,
# which the allow rules in settings.json expect, and does nothing where TS_AUTHKEY or tailscale is missing, outside a
# disposable session, or where the daemon is already up, keeps its identity in memory and reaches benchpress.
#
# It manages the daemon only in a session positively identified as disposable: Claude Code's cloud
# (CLAUDE_CODE_REMOTE=true), or an environment that sets BUSTER_TAILNET_DISPOSABLE=1 (Codex's cloud environment must, to
# join). Neither process 1's name nor being in a container says that a host's daemon or its state is disposable. On any
# other machine the daemon and its state belong to the host: the hook leaves them alone, whatever TS_AUTHKEY says.
#
# The environment may have started a daemon on the default socket already,
# without HTTPS_PROXY in its environment: it dials the control server directly,
# which the egress gateway refuses (403), so it stays logged out. Such a daemon
# is stopped and replaced by one started here, which inherits the session's
# proxy. A daemon on another socket is left alone; a daemon whose --socket names
# the default one, in any of the flag's forms or through /var/run's link to
# /run, is on the default socket.
#
# A daemon can also be up but stale: logged in from an earlier start, its control connection failing since (#1282's
# session: the proxy given as an IP literal, which its control dialer refused), so that no peer answers it although
# `tailscale status` lists them. Where the control server lists benchpress online and it doesn't answer a ping, even
# through a DERP relay, the daemon is replaced the same way.
#
# Each session's daemon needs a machine key of its own. The environment's image was saved after this hook had run,
# with the state file of the daemon it started, so a daemon that keeps its state in /var/lib/tailscale starts every
# session with the same machine key. The control server takes them all for one node: each session's registration
# replaces the others' node and disco keys, the node's name changes every minute or so, and no peer answers any of
# them. So the daemon keeps its state in memory (`--state=mem:`, which also makes the node ephemeral, and never reads
# the file), and a daemon on the default socket that keeps its state on disk is replaced. No state file is removed.
#
# BUSTER_TAILNET_PROC, BUSTER_TAILNET_SOCKET and BUSTER_TAILNET_LOG stand for /proc, the default socket and the log for
# .claude/hooks/test-join-tailnet.sh, which runs this with stand-ins for the daemon and its tools.

proc=${BUSTER_TAILNET_PROC:-/proc}
socket=${BUSTER_TAILNET_SOCKET:-/var/run/tailscale/tailscaled.sock}
log=${BUSTER_TAILNET_LOG:-/var/lib/tailscale/log}
default_socket=$(realpath -m "$socket")

disposable()
{
    [ "${CLAUDE_CODE_REMOTE:-}" = true ] || [ "${BUSTER_TAILNET_DISPOSABLE:-}" = 1 ]
}

# The value of tailscaled's flag NAME in process PID's command line, in any of Go's forms (-NAME=V, --NAME=V, -NAME V,
# --NAME V). Fails where the flag isn't given.
daemon_flag()
{
    tr '\0' '\n' < "$proc/$1/cmdline" 2> /dev/null | awk -v name="$2" '
        found == 1 { print; found = 2; exit }
        $0 == "-" name || $0 == "--" name { found = 1; next }
        index($0, "-" name "=") == 1 { print substr($0, length(name) + 3); found = 2; exit }
        index($0, "--" name "=") == 1 { print substr($0, length(name) + 4); found = 2; exit }
        END { exit found != 2 }'
}

# The tailscaled processes on the default socket: those that name no socket, and those whose socket is it.
default_daemons()
{
    local pid path
    for pid in $(pgrep -x tailscaled); do
        path=$(daemon_flag "$pid" socket) || path=$socket
        if [ "$(realpath -m "$path")" = "$default_socket" ]; then
            echo "$pid"
        fi
    done
}

state_on_disk()
{
    local pid found=1
    for pid in $(default_daemons); do
        if [ "$(daemon_flag "$pid" state)" != mem: ]; then
            found=0
        fi
    done
    return $found
}

benchpress_unreachable()
{
    tailscale --socket="$socket" status 2> /dev/null | awk '$2 == "benchpress" && !/offline/ { found = 1 } END { exit !found }' &&
        ! timeout 30 tailscale --socket="$socket" ping -c 3 --timeout=5s --until-direct=false benchpress > /dev/null 2>&1
}

if [ -n "$TS_AUTHKEY" ] && command -v tailscaled > /dev/null && disposable &&
    { ! tailscale --socket="$socket" status > /dev/null 2>&1 || state_on_disk || benchpress_unreachable; }; then
    daemons=$(default_daemons)
    for pid in $daemons; do
        kill $pid 2> /dev/null
        for i in $(seq 20); do
            kill -0 $pid 2> /dev/null || break
            sleep 0.25
        done
        kill -9 $pid 2> /dev/null
    done
    alive=0
    for pid in $daemons; do
        if kill -0 $pid 2> /dev/null; then
            alive=1
        fi
    done
    # A daemon that outlived its signals still owns the socket: leave it be rather than start a second one.
    if [ $alive = 0 ]; then
        rm -f "$socket"
        mkdir -p "$(dirname "$socket")" "$(dirname "$log")"
        nohup tailscaled --tun=userspace-networking --socks5-server=localhost:1055 --socket="$socket" --state=mem: > "$log" 2>&1 &
        for i in $(seq 40); do
            [ -S "$socket" ] && break
            sleep 0.5
        done
        timeout 60 tailscale --socket="$socket" up --authkey="$TS_AUTHKEY" --hostname="claude-cloud-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')" --ssh=false > /dev/null 2>&1
    fi
fi
exit 0
