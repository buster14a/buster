#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# An outage drill's network (.github/workflows/hosted-inputs-drill.yml, #2275), on a disposable hosted VM:
#
#   hosts-down.sh [--apt] HOST...
#
# Every HOST resolves to the loopback address alone (/etc/hosts, whose earlier entries for it, such as a preparation's
# DNS-over-HTTPS pins, are removed first: glibc returns every matching line), so that each request to it is refused.
# --apt adds every host that apt's sources and mirror lists name, Ubuntu's archive, security and snapshot hosts, and a firewall
# rule that refuses plain HTTP, which Ubuntu's mirrors serve, to every address but the platform's link-local ones. Each
# host must then be unreachable by HTTPS and HTTP, or this fails. BUSTER_HOSTS_FILE, for a check of the table alone,
# edits that file instead, and stops there.
set -euo pipefail
apt=false
if [ "${1:-}" = --apt ]; then
    apt=true
    shift
fi
hosts=("$@")
if [ "$apt" = true ]; then
    mapfile -t -O "${#hosts[@]}" hosts < <(
        for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/*; do
            if [ -f "$file" ]; then
                { grep -vE '^[[:space:]]*#' "$file" || true; } | { grep -oE 'https?://[^/[:space:]"]+' || true; }
            fi
        done | sed -E 's|^https?://||; s|^.*@||; s|:[0-9]+$||'
    )
    hosts+=(archive.ubuntu.com security.ubuntu.com azure.archive.ubuntu.com ports.ubuntu.com snapshot.ubuntu.com)
fi
mapfile -t hosts < <(printf '%s\n' "${hosts[@]}" | LC_ALL=C sort -u)
[ "${#hosts[@]}" -gt 0 ] || { echo 'hosts-down.sh: no host to take down' >&2; exit 2; }
for host in "${hosts[@]}"; do
    [[ "$host" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ ]] || { echo "hosts-down.sh: invalid host $host" >&2; exit 2; }
done
hosts_file=${BUSTER_HOSTS_FILE:-/etc/hosts}
table=$(mktemp)
trap 'rm -f -- "$table"' EXIT
# Each line keeps its address, its comment and its other names; a line left with no name goes.
awk -v hosts="${hosts[*]}" '
    BEGIN { count = split(hosts, list, " "); for (i = 1; i <= count; i++) down[tolower(list[i])] = 1 }
    /^[[:space:]]*(#|$)/ { print; next }
    {
        line = $1
        names = 0
        for (i = 2; i <= NF; i++) {
            if ($i ~ /^#/) {
                for (; i <= NF; i++) line = line " " $i
                break
            }
            if (!(tolower($i) in down)) {
                line = line " " $i
                names++
            }
        }
        if (names > 0) print line
    }' "$hosts_file" > "$table"
for host in "${hosts[@]}"; do
    printf '127.0.0.1 %s\n::1 %s\n' "$host" "$host" >> "$table"
done
# Into the same file, which may be a mount point, rather than a new one in its place.
sudo cp -- "$table" "$hosts_file"
if [ -n "${BUSTER_HOSTS_FILE:-}" ]; then
    exit 0
fi
if [ "$apt" = true ]; then
    sudo iptables -I OUTPUT -p tcp --dport 80 ! -d 169.254.0.0/16 -j REJECT --reject-with tcp-reset
    sudo ip6tables -I OUTPUT -p tcp --dport 80 -j REJECT --reject-with tcp-reset || echo 'hosts-down.sh: no ip6tables; the hosts above still resolve to ::1 alone'
fi
for host in "${hosts[@]}"; do
    for scheme in https http; do
        if curl --silent --max-time 10 --output /dev/null "$scheme://$host/"; then
            echo "::error::$scheme://$host/ is still reachable"
            exit 1
        fi
    done
done
printf 'Down: %s\n' "${hosts[*]}"
