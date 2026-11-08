#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Shared release acquisition: verified cache, then the digest-pinned GHCR mirror,
# then the original upstream URL. Every source is checked against the lock's
# SHA-256 before use. Only prepare may recover an absent/corrupt hosted handoff;
# get never falls back when BUSTER_HOSTED_RELEASES is set (even empty).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
lock=${BUSTER_HOSTED_LOCK:-$here/hosted-tools.lock}
[[ $(head -n 1 "$lock") = '# buster-hosted-inputs-v1 linux-x86_64 ubuntu-26.04' ]] || { echo 'hosted-release: wrong lock schema/platform' >&2; exit 1; }
declare -A versions=() digests=() urls=()
names=()
mirror_registry='' mirror_repository='' mirror_manifest='' mirror_state=unknown
fail() { printf 'hosted-release: %s\n' "$*" >&2; exit 1; }
log() { printf 'hosted-release: %s\n' "$*" >&2; }
while read -r name version digest url extra; do
    [[ -z "$name" || "$name" = \#* ]] && continue
    if [[ "$name" = mirror ]]; then
        # mirror REGISTRY/REPOSITORY sha256:MANIFEST|unpublished. The manifest
        # digest is reviewed into this lock after a trusted publication.
        [[ -z "$mirror_registry" && "$version" =~ ^ghcr\.io/[a-z0-9][a-z0-9._-]*(/[a-z0-9][a-z0-9._-]*)+$ && ( "$digest" =~ ^sha256:[0-9a-f]{64}$ || "$digest" = unpublished ) && -z "$url" ]] || fail 'invalid mirror record'
        mirror_registry=${version%%/*} mirror_repository=${version#*/}
        [[ "$digest" = unpublished ]] || mirror_manifest=${digest#sha256:}
        continue
    fi
    [[ "$name" =~ ^[a-z][a-z0-9-]*$ && "$version" =~ ^[0-9]+(\.[0-9]+)+$ && "$digest" =~ ^[0-9a-f]{64}$ && "$url" = https://* && -z "$extra" ]] || fail 'invalid lock record'
    [[ ! -v versions[$name] ]] || fail "duplicate component $name"
    versions[$name]=$version digests[$name]=$digest urls[$name]=$url
    names+=("$name")
done < "$lock"
for name in llvm llvm-license cmake ninja mold wild; do
    [[ -v versions[$name] ]] || fail "missing component $name"
done
[[ -n "$mirror_registry" ]] || fail 'missing mirror record'
# Private scratch space for registry tokens; never part of a published directory.
scratch=$(umask 077 && mktemp -d)
temporary=''
trap 'rm -rf -- "$scratch" ${temporary:+"$temporary"}' EXIT
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
    log "component=$name version=${versions[$name]} source=upstream"
    # timeout also bounds an in-flight request: curl's retry-max-time does not.
    # Only HTTPS sources are allowed; deterministic file:// fixtures use a fake curl.
    timeout --kill-after=5s 300s curl --fail --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 90 --speed-time 30 --speed-limit 1024 \
        --retry 8 --retry-all-errors --retry-max-time 240 --silent --show-error \
        --write-out "hosted-release: component=$name http=%{http_code} last_bytes=%{size_download} last_seconds=%{time_total}\n" \
        --output "$output" "${urls[$name]}" >&2 || return 1
    verify_file "$name" "$output" || { printf 'hosted-release: integrity failure for %s\n' "$name" >&2; return 1; }
}
# Registry request: prints only the final HTTP status. The URL is never logged:
# blob reads redirect to signed storage URLs.
registry()
{
    local output=$1 url=$2 deadline=$3
    shift 3
    timeout --kill-after=5s "${deadline}s" curl --fail --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time "$((deadline - 10))" --speed-time 30 --speed-limit 1024 \
        --silent --show-error --write-out '%{http_code}' --output "$output" "$@" "$url"
}
reason()
{
    case "${1:-}" in
        404) printf missing ;;
        401|403) printf denied ;;
        ''|000) printf transport ;;
        *) printf 'http-%s' "$1" ;;
    esac
}
mirror_ready()
{
    local code token base
    if [[ "$mirror_state" != unknown ]]; then
        [[ "$mirror_state" = ready ]]
        return
    fi
    mirror_state=unavailable
    if [[ -z "$mirror_manifest" ]]; then
        log 'source=mirror unavailable reason=unpublished'
        return 1
    fi
    if [[ "${BUSTER_HOSTED_MIRROR:-on}" = off ]]; then
        log 'source=mirror unavailable reason=disabled'
        return 1
    fi
    base="https://$mirror_registry"
    # Anonymous, secret-free read token: the package must be public.
    code=$(registry "$scratch/token.json" "$base/token?service=$mirror_registry&scope=repository:$mirror_repository:pull" 60 --retry 3 --retry-all-errors --retry-max-time 40) || { log "source=mirror unavailable reason=$(reason "$code") stage=token http=${code:-none}"; return 1; }
    token=$(sed -n 's/.*"token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$scratch/token.json")
    rm -f -- "$scratch/token.json"
    [[ -n "$token" ]] || { log 'source=mirror unavailable reason=malformed-token stage=token'; return 1; }
    printf 'Authorization: Bearer %s\n' "$token" > "$scratch/header"
    code=$(registry "$scratch/manifest" "$base/v2/$mirror_repository/manifests/sha256:$mirror_manifest" 60 --retry 3 --retry-all-errors --retry-max-time 40 \
        -H @"$scratch/header" -H 'Accept: application/vnd.oci.image.manifest.v1+json') || { log "source=mirror unavailable reason=$(reason "$code") stage=manifest http=${code:-none}"; return 1; }
    printf '%s  %s\n' "$mirror_manifest" "$scratch/manifest" | sha256sum --check --status || fail 'mirror integrity failure: manifest does not match the pinned digest'
    mirror_state=ready
}
# Integrity failures are fatal, never a silent fallback; availability failures
# (unpublished, denied, missing, transport) fall back to upstream.
mirror_fetch()
{
    local name=$1 output=$2 code
    mirror_ready || return 1
    grep -q "\"digest\":\"sha256:${digests[$name]}\"" "$scratch/manifest" || fail "mirror integrity failure: the pinned manifest does not list $name"
    log "component=$name version=${versions[$name]} source=mirror"
    code=$(registry "$output" "https://$mirror_registry/v2/$mirror_repository/blobs/sha256:${digests[$name]}" 300 --retry 3 --retry-all-errors --retry-max-time 120 \
        -H @"$scratch/header") || { log "component=$name source=mirror unavailable reason=$(reason "$code") http=${code:-none}"; rm -f -- "$output"; return 1; }
    verify_file "$name" "$output" || fail "mirror integrity failure for $name"
    log "component=$name source=mirror verified=true bytes=$(stat -c %s -- "$output")"
}
acquire()
{
    mirror_fetch "$1" "$2" || download "$1" "$2"
}
publish()
{
    local directory=$1 base code location name size layers='' manifest tag config
    : "${BUSTER_MIRROR_PUSH_TOKEN:?publish needs the trusted workflow token}"
    : "${GITHUB_ACTOR:?publish needs the workflow actor}"
    verify_directory "$directory" || fail 'refusing to publish unverified inputs'
    base="https://$mirror_registry"
    printf 'user = "%s:%s"\n' "$GITHUB_ACTOR" "$BUSTER_MIRROR_PUSH_TOKEN" > "$scratch/auth"
    code=$(registry "$scratch/token.json" "$base/token?service=$mirror_registry&scope=repository:$mirror_repository:pull,push" 60 --config "$scratch/auth") || fail "publish token failed: $(reason "$code")"
    rm -f -- "$scratch/auth"
    printf 'Authorization: Bearer %s\n' "$(sed -n 's/.*"token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$scratch/token.json")" > "$scratch/header"
    rm -f -- "$scratch/token.json"
    printf '{}' > "$scratch/config"
    config=$(sha256sum "$scratch/config" | cut -c 1-64)
    put_blob()
    {
        local file=$1 digest=$2
        code=$(registry /dev/null "$base/v2/$mirror_repository/blobs/sha256:$digest" 60 --head -H @"$scratch/header") && { log "publish blob=$digest present"; return 0; }
        [[ "$code" = 404 ]] || fail "publish blob probe failed: $(reason "$code")"
        code=$(registry /dev/null "$base/v2/$mirror_repository/blobs/uploads/" 60 -X POST -D "$scratch/headers" -H @"$scratch/header") || fail "publish upload start failed: $(reason "$code")"
        location=$(sed -n 's/^[Ll]ocation:[[:space:]]*\([^[:space:]]*\).*/\1/p' "$scratch/headers" | tail -n 1)
        [[ "$location" = /* ]] && location="$base$location"
        code=$(registry /dev/null "$location" 900 -X PATCH -D "$scratch/headers" -H @"$scratch/header" -H 'Content-Type: application/octet-stream' -T "$file") || fail "publish upload failed: $(reason "$code")"
        location=$(sed -n 's/^[Ll]ocation:[[:space:]]*\([^[:space:]]*\).*/\1/p' "$scratch/headers" | tail -n 1)
        [[ "$location" = /* ]] && location="$base$location"
        [[ "$location" = *\?* ]] && location="$location&digest=sha256:$digest" || location="$location?digest=sha256:$digest"
        code=$(registry /dev/null "$location" 60 -X PUT -H @"$scratch/header" -H 'Content-Length: 0') || fail "publish upload commit failed: $(reason "$code")"
        log "publish blob=$digest uploaded bytes=$(stat -c %s -- "$file")"
    }
    put_blob "$scratch/config" "$config"
    for name in "${names[@]}"; do
        put_blob "$directory/$name" "${digests[$name]}"
        size=$(stat -c %s -- "$directory/$name")
        layers+="${layers:+,}{\"mediaType\":\"application/octet-stream\",\"digest\":\"sha256:${digests[$name]}\",\"size\":$size,\"annotations\":{\"org.opencontainers.image.title\":\"$name\",\"org.opencontainers.image.version\":\"${versions[$name]}\"}}"
    done
    manifest="{\"schemaVersion\":2,\"mediaType\":\"application/vnd.oci.image.manifest.v1+json\",\"config\":{\"mediaType\":\"application/vnd.buster.hosted-inputs.config.v1+json\",\"digest\":\"sha256:$config\",\"size\":2},\"layers\":[$layers],\"annotations\":{\"org.opencontainers.image.source\":\"https://github.com/buster14a/buster\",\"org.opencontainers.image.description\":\"Verified original Buster hosted CI release inputs\"}}"
    printf '%s' "$manifest" > "$scratch/manifest"
    # Deterministic from the pinned inputs alone: republishing after the pin is promoted is a no-op.
    tag="inputs-$(grep -v '^mirror ' "$lock" | sha256sum | cut -c 1-16)"
    code=$(registry /dev/null "$base/v2/$mirror_repository/manifests/$tag" 60 -X PUT -H @"$scratch/header" -H 'Content-Type: application/vnd.oci.image.manifest.v1+json' --data-binary @"$scratch/manifest") || fail "publish manifest failed: $(reason "$code")"
    digest=$(sha256sum "$scratch/manifest" | cut -c 1-64)
    # Read back by digest; the reviewed lock pins this digest, never the tag.
    code=$(registry "$scratch/readback" "$base/v2/$mirror_repository/manifests/sha256:$digest" 60 -H @"$scratch/header" -H 'Accept: application/vnd.oci.image.manifest.v1+json') || fail "publish read-back failed: $(reason "$code")"
    cmp -s "$scratch/manifest" "$scratch/readback" || fail 'publish read-back differs'
    log "publish tag=$tag manifest=sha256:$digest"
    printf 'mirror %s/%s sha256:%s\n' "$mirror_registry" "$mirror_repository" "$digest"
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
        start=$SECONDS
        for name in "${names[@]}"; do
            if [[ ! -L "$directory" ]] && verify_file "$name" "$directory/$name"; then
                log "component=$name source=cache verified=true"
                cp -- "$directory/$name" "$temporary/$name"
            else
                acquire "$name" "$temporary/$name" || fail "acquisition failed for $name"
            fi
        done
        verify_directory "$temporary" || fail 'prepared inputs failed verification'
        rm -rf -- "$directory"
        mv -- "$temporary" "$directory"
        temporary=''
        log "prepared=$directory seconds=$((SECONDS-start)) bytes=$(du -sb -- "$directory" | cut -f1)"
        ;;
    get)
        [[ $# = 3 && ${2:-} =~ ^[a-z][a-z0-9-]*$ ]] && [[ -v "versions[$2]" ]] || fail 'usage: get COMPONENT OUTPUT'
        name=$2 output=$3
        mkdir -p -- "$(dirname -- "$output")"
        temporary=$(mktemp "${output}.download.XXXXXXXX")
        if [[ ${BUSTER_HOSTED_RELEASES+x} ]]; then
            [[ -n "$BUSTER_HOSTED_RELEASES" && ! -L "$BUSTER_HOSTED_RELEASES" ]] || fail 'invalid hosted handoff directory'
            verify_file "$name" "$BUSTER_HOSTED_RELEASES/$name" || fail "invalid hosted handoff for $name; refusing per-shard upstream fallback"
            cp -- "$BUSTER_HOSTED_RELEASES/$name" "$temporary"
        else
            # Standalone/self-hosted callers (build-pgo, the 9700X) acquire directly.
            acquire "$name" "$temporary" || fail "acquisition failed for $name"
        fi
        verify_file "$name" "$temporary" || fail "copied input changed for $name"
        mv -- "$temporary" "$output"
        temporary=''
        ;;
    publish)
        [[ $# = 2 ]] || fail 'usage: publish DIRECTORY'
        publish "$2"
        ;;
    *) fail 'usage: hosted-release.sh version|digest COMPONENT | verify|prepare|publish DIRECTORY | get COMPONENT OUTPUT' ;;
esac
