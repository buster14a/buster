#!/bin/bash
# SessionStart hook: joins the tailnet so that sessions reach benchpress (the
# 9700X) with `tailscale ssh` without asking. It uses tailscaled's default
# socket, which the allow rules in settings.json expect, and does nothing where
# TS_AUTHKEY or tailscale is missing or the daemon is already up.

if [ -n "$TS_AUTHKEY" ] && command -v tailscaled > /dev/null && ! tailscale status > /dev/null 2>&1; then
    mkdir -p /var/run/tailscale /var/lib/tailscale
    nohup tailscaled --tun=userspace-networking --socks5-server=localhost:1055 > /var/lib/tailscale/log 2>&1 &
    for i in $(seq 40); do
        [ -S /var/run/tailscale/tailscaled.sock ] && break
        sleep 0.5
    done
    timeout 60 tailscale up --authkey="$TS_AUTHKEY" --hostname="claude-cloud-$(head -c 4 /dev/urandom | od -An -tx1 | tr -d ' \n')" --ssh=false > /dev/null 2>&1
fi
exit 0
