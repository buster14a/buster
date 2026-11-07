#!/bin/bash
# SessionStart hook: joins the tailnet so that sessions reach benchpress (the 9700X) with
# `tailscale ssh david@benchpress` without asking (docs/commands/benchpress.md). It uses tailscaled's default socket,
# which the allow rules in settings.json expect, and does nothing where TS_AUTHKEY or tailscale is missing or the
# daemon is already up and reaches benchpress.
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
# The daemon keeps its node key in memory (`--state=mem:`), as the environment's setup script starts its own. A key in
# /var/lib/tailscale/tailscaled.state outlives the container in its saved filesystem, and #653's session saw its node
# used from elsewhere, most likely by a container started from such a copy: its IP stayed the same while its hostname
# changed every few seconds to names the session never chose, and pings to benchpress failed until the state file was
# moved aside. A key left by an earlier hook is removed when the daemon is replaced. Each daemon start then registers a
# new node, under a new random hostname as before.

benchpress_unreachable()
{
    tailscale status 2> /dev/null | awk '$2 == "benchpress" && !/offline/ { found = 1 } END { exit !found }' &&
        ! timeout 30 tailscale ping -c 3 --timeout=5s --until-direct=false benchpress > /dev/null 2>&1
}

if [ -n "$TS_AUTHKEY" ] && command -v tailscaled > /dev/null && { ! tailscale status > /dev/null 2>&1 || benchpress_unreachable; }; then
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
    nohup tailscaled --tun=userspace-networking --state=mem: --socks5-server=localhost:1055 > /var/lib/tailscale/log 2>&1 &
    for i in $(seq 40); do
        [ -S /var/run/tailscale/tailscaled.sock ] && break
        sleep 0.5
    done
    timeout 60 tailscale up --authkey="$TS_AUTHKEY" --hostname="claude-cloud-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')" --ssh=false > /dev/null 2>&1
fi
exit 0
