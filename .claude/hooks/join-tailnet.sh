#!/bin/bash
# SessionStart hook: joins the tailnet so that sessions reach benchpress (the 9700X) with
# `tailscale ssh david@benchpress` without asking (docs/commands/benchpress.md). It uses tailscaled's default socket,
# which the allow rules in settings.json expect, and does nothing where TS_AUTHKEY or tailscale is missing or the
# daemon is already up, keeps its identity in memory and reaches benchpress.
#
# The environment may have started a daemon on the default socket already,
# without HTTPS_PROXY in its environment: it dials the control server directly,
# which the egress gateway refuses (403), so it stays logged out. Such a daemon
# is stopped and replaced by one started here, which inherits the session's
# proxy. A daemon on a socket of its own (`--socket=`) is left alone.
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
# them. So the daemon keeps its state in memory (`--state=mem:`, which also makes the node ephemeral), the image's
# state file is removed, and a daemon on the default socket that keeps its state on disk is replaced.

benchpress_unreachable()
{
    tailscale status 2> /dev/null | awk '$2 == "benchpress" && !/offline/ { found = 1 } END { exit !found }' &&
        ! timeout 30 tailscale ping -c 3 --timeout=5s --until-direct=false benchpress > /dev/null 2>&1
}

state_on_disk()
{
    local found=1
    for pid in $(pgrep -x tailscaled); do
        if ! tr '\0' '\n' < /proc/$pid/cmdline | grep -q -e '^--socket' -e '^--state=mem:$'; then
            found=0
        fi
    done
    return $found
}

if [ -n "$TS_AUTHKEY" ] && command -v tailscaled > /dev/null &&
    { ! tailscale status > /dev/null 2>&1 || state_on_disk || benchpress_unreachable; }; then
    for pid in $(pgrep -x tailscaled); do
        if ! tr '\0' '\n' < /proc/$pid/cmdline | grep -q '^--socket'; then
            kill $pid 2> /dev/null
            for i in $(seq 20); do
                kill -0 $pid 2> /dev/null || break
                sleep 0.25
            done
            kill -9 $pid 2> /dev/null
        fi
    done
    rm -f /var/run/tailscale/tailscaled.sock /var/lib/tailscale/tailscaled.state
    mkdir -p /var/run/tailscale /var/lib/tailscale
    nohup tailscaled --tun=userspace-networking --socks5-server=localhost:1055 --state=mem: > /var/lib/tailscale/log 2>&1 &
    for i in $(seq 40); do
        [ -S /var/run/tailscale/tailscaled.sock ] && break
        sleep 0.5
    done
    timeout 60 tailscale up --authkey="$TS_AUTHKEY" --hostname="claude-cloud-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')" --ssh=false > /dev/null 2>&1
fi
exit 0
