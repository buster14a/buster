#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Shared release acquisition. Only prepare may recover an absent/corrupt hosted
# handoff. get never falls back when BUSTER_HOSTED_RELEASES is set (even empty).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
[[ $(head -n 1 "$here/hosted-tools.lock") = '# buster-hosted-inputs-v1 linux-x86_64 ubuntu-26.04' ]] || { echo 'hosted-release: wrong lock schema/platform' >&2; exit 1; }
declare -A versions=() digests=() urls=()
names=()
fail() { printf 'hosted-release: %s\n' "$*" >&2; exit 1; }
while read -r name version digest url extra; do
    [[ -z "$name" || "$name" = \#* ]] && continue
    [[ "$name" =~ ^[a-z][a-z0-9-]*$ && "$version" =~ ^[0-9]+(\.[0-9]+)+$ && "$digest" =~ ^[0-9a-f]{64}$ && "$url" = https://* && -z "$extra" ]] || fail 'invalid lock record'
    [[ ! -v versions[$name] ]] || fail "duplicate component $name"
    versions[$name]=$version digests[$name]=$digest urls[$name]=$url
    names+=("$name")
done < "$here/hosted-tools.lock"
for name in llvm llvm-license cmake ninja mold wild; do
    [[ -v versions[$name] ]] || fail "missing component $name"
done
verify_file()
{
    [[ -f "$2" && ! -L "$2" ]] && printf '%s  %s\n' "${digests[$1]}" "$2" | sha256sum --check --status
}
verify_directory()
{
    local directory=$1 name
    [[ -d "$directory" && ! -L "$directory" ]] || return 1
    for name in "${names[@]}"; do
        verify_file "$name" "$directory/$name" || { printf 'hosted-release: invalid/missing %s\n' "$name" >&2; return 1; }
    done
}
download()
{
    local name=$1 output=$2
    printf 'hosted-release: component=%s version=%s source=upstream\n' "$name" "${versions[$name]}" >&2
    # timeout also bounds an in-flight request: curl's retry-max-time does not.
    # Only HTTPS sources are allowed; deterministic file:// fixtures use a fake curl.
    timeout --kill-after=5s 300s curl --fail --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 90 --speed-time 30 --speed-limit 1024 \
        --retry 8 --retry-all-errors --retry-max-time 240 --silent --show-error \
        --write-out "hosted-release: component=$name http=%{http_code} last_bytes=%{size_download} last_seconds=%{time_total}\n" \
        --output "$output" "${urls[$name]}" >&2 || return 1
    verify_file "$name" "$output" || { printf 'hosted-release: integrity failure for %s\n' "$name" >&2; return 1; }
}
command=${1:-}
case "$command" in
    version|digest)
        [[ $# = 2 && ${2:-} =~ ^[a-z][a-z0-9-]*$ ]] && [[ -v "versions[$2]" ]] || fail 'unknown component'
        if [[ "$command" = version ]]; then printf '%s\n' "${versions[$2]}"; else printf '%s\n' "${digests[$2]}"; fi
        ;;
    verify)
        [[ $# = 2 ]] || fail 'usage: verify DIRECTORY'
        verify_directory "$2"
        ;;
    prepare)
        [[ $# = 2 && -n "$2" && "$2" != / ]] || fail 'usage: prepare DIRECTORY'
        directory=$2
        mkdir -p -- "$(dirname -- "$directory")"
        exec 9> "${directory}.lock"
        flock 9
        temporary=$(mktemp -d "${directory}.prepare.XXXXXXXX")
        trap 'rm -rf -- "$temporary"' EXIT
        start=$SECONDS
        for name in "${names[@]}"; do
            if [[ ! -L "$directory" ]] && verify_file "$name" "$directory/$name"; then
                printf 'hosted-release: component=%s source=cache verified=true\n' "$name" >&2
                cp -- "$directory/$name" "$temporary/$name"
            else
                download "$name" "$temporary/$name" || fail "acquisition failed for $name"
            fi
        done
        verify_directory "$temporary" || fail 'prepared inputs failed verification'
        rm -rf -- "$directory"
        mv -- "$temporary" "$directory"
        trap - EXIT
        printf 'hosted-release: prepared=%s seconds=%s bytes=%s\n' "$directory" "$((SECONDS-start))" "$(du -sb -- "$directory" | cut -f1)" >&2
        ;;
    get)
        [[ $# = 3 && ${2:-} =~ ^[a-z][a-z0-9-]*$ ]] && [[ -v "versions[$2]" ]] || fail 'usage: get COMPONENT OUTPUT'
        name=$2 output=$3
        mkdir -p -- "$(dirname -- "$output")"
        temporary=$(mktemp "${output}.download.XXXXXXXX")
        trap 'rm -f -- "$temporary"' EXIT
        if [[ ${BUSTER_HOSTED_RELEASES+x} ]]; then
            [[ -n "$BUSTER_HOSTED_RELEASES" && ! -L "$BUSTER_HOSTED_RELEASES" ]] || fail 'invalid hosted handoff directory'
            verify_file "$name" "$BUSTER_HOSTED_RELEASES/$name" || fail "invalid hosted handoff for $name; refusing per-shard upstream fallback"
            cp -- "$BUSTER_HOSTED_RELEASES/$name" "$temporary"
        else
            # Legacy standalone/self-hosted callers retain their download path.
            download "$name" "$temporary" || fail "acquisition failed for $name"
        fi
        verify_file "$name" "$temporary" || fail "copied input changed for $name"
        mv -- "$temporary" "$output"
        trap - EXIT
        ;;
    *) fail 'usage: hosted-release.sh version|digest COMPONENT | verify|prepare DIRECTORY | get COMPONENT OUTPUT' ;;
esac
