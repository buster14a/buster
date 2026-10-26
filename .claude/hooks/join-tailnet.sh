#!/bin/bash
# SessionStart hook: joins the tailnet so that sessions reach benchpress (the
# 9700X) with `tailscale ssh` without asking. It uses tailscaled's default
# socket, which the allow rules in settings.json expect, and does nothing where
# TS_AUTHKEY or tailscale is missing or the daemon is already up.
#
# The environment may have started a daemon on the default socket already,
# without HTTPS_PROXY in its environment: it dials the control server directly,
# which the egress gateway refuses (403), so it stays logged out. Such a daemon
# is stopped and replaced by one started here, which inherits the session's
# proxy. A daemon on a socket of its own (`--socket=`) is left alone.

if [ -n "$TS_AUTHKEY" ] && command -v tailscaled > /dev/null && ! tailscale status > /dev/null 2>&1; then
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
    rm -f /var/run/tailscale/tailscaled.sock
    mkdir -p /var/run/tailscale /var/lib/tailscale
    nohup tailscaled --tun=userspace-networking --socks5-server=localhost:1055 > /var/lib/tailscale/log 2>&1 &
    for i in $(seq 40); do
        [ -S /var/run/tailscale/tailscaled.sock ] && break
        sleep 0.5
    done
    timeout 60 tailscale up --authkey="$TS_AUTHKEY" --hostname="claude-cloud-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')" --ssh=false > /dev/null 2>&1
fi
exit 0
