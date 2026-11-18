#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The hosted jobs' external inputs besides the release inputs (#2275; docs/hosted-toolchain.md, "Offline inputs"): the
# apt packages setup-hosted.sh installs, the frozen corpus
# (fetch-corpus.sh), c-testsuite's and SQLite's pinned commits (fetch-c-testsuite.sh, fetch-sqlite.sh), and Lua's release
# archive (this script's lua command). The
# preparation job acquires them once per run into a bundle, ROOT, which every hosted job then reads with no network:
#
#   ROOT/apt/MANIFEST, ROOT/apt/*.deb  The .debs that apt-get --download-only fetched for the package list on this
#                                      runner image, those of the image's own packages that they pin to an exact
#                                      version (carried: see pinned_outside), and what MANIFEST records of them and of
#                                      the image's own packages.
#   ROOT/ci                            BUSTER_CI_CACHE's layout, as on the 9700X: cached-download.sh's downloads/DIGEST
#                                      and the fetch scripts' NAME.git mirrors holding refs/pinned/COMMIT.
#
#   hosted-offline.sh keys             The bundle's two cache keys, for GITHUB_OUTPUT: apt-key, from the package list and
#                                      this runner's image (ImageOS, ImageVersion), and ci-key, from the scripts that pin
#                                      the rest (their URLs, commits and SHA-256 digests).
#   hosted-offline.sh prepare ROOT     Keeps each input that ROOT already holds and that checks out without the network,
#                                      and acquires the others once, with retries: the package lists and the closure's
#                                      .debs on this runner, and the rest by running their own scripts with
#                                      BUSTER_CI_CACHE set to ROOT/ci, which also proves that each one extracts. Logs
#                                      every input's source.
#   hosted-offline.sh verify ROOT      A consumer's check of the handoff before anything is installed: the manifest's
#                                      format, its package list, this runner's release and architecture, every .deb's
#                                      SHA-256, and no file in ROOT/apt that the manifest doesn't list. ROOT/ci's inputs
#                                      are checked on use, by the scripts that read them.
#   hosted-offline.sh apt-install ROOT setup-hosted.sh's install: the manifest's .debs by apt-get --no-download, resolved
#                                      against the installed packages alone (no package lists), then every recorded
#                                      version compared with dpkg's. The step summary lists them. On another image of
#                                      a rollout a carried package moves to the bundle's version, up or down.
#   hosted-offline.sh lua DIRECTORY    Reads the pinned Lua archive through cached-download.sh and extracts its source
#                                      tree into DIRECTORY; hosted consumers require its verified offline copy.
#
# A consumer never reaches the network for these inputs: a missing file or a hash that doesn't match fails, and nothing
# is fetched again (BUSTER_CI_OFFLINE=1 makes the scripts that read ROOT/ci refuse to). Only `prepare` acquires.
set -euo pipefail
shopt -s inherit_errexit

here=$(cd -- "$(dirname -- "$0")" && pwd)
# 2: the bundle carries the image's packages that its .debs pin exactly (#2974), so a version 1 bundle is resolved again.
format='buster-hosted-apt 2'
# tools/ci/setup-hosted.sh's packages. The bundle holds what this runner image lacks of their closure, so it is the
# image's: its cache key names the image. No valgrind: nothing hosted has run it since the scalar host went (#2924), and
# its libc6-dbg pins the image's libc6 exactly, which dropped merge-queue candidates during an image rollout (#2974).
packages=(gcc g++ binutils libc6-dev gdb tcc zstd linux-perf qemu-user libzstd-dev libxml2-dev zlib1g-dev tcl8.6-dev)
# The inputs under ROOT/ci, each acquired and checked by its own script, and the scripts whose contents pin them.
ci_inputs=(corpus c-testsuite sqlite lua)
ci_scripts=(fetch-corpus.sh fetch-c-testsuite.sh fetch-sqlite.sh cached-download.sh hosted-offline.sh)
# The pause before a failed acquisition's next attempt, times the attempt's number: 20 and 40 s. The fixtures set it
# to 0.
retry_seconds=${BUSTER_HOSTED_RETRY_SECONDS:-20}
# Where unresolvable apt hosts are pinned; the fixtures name their own file.
hosts_file=${BUSTER_HOSTED_HOSTS_FILE:-/etc/hosts}
[[ "$retry_seconds" =~ ^[0-9]+$ ]] || { echo 'hosted-offline: BUSTER_HOSTED_RETRY_SECONDS must be a number of seconds' >&2; exit 1; }
temporary=''
trap 'rm -rf -- ${temporary:+"$temporary"}' EXIT

log()
{
    printf 'hosted-offline: %s\n' "$*" >&2
}

# On standard error, which the runner reads workflow commands from too, so that a failure inside a command
# substitution still shows.
die()
{
    printf '::error title=Offline hosted inputs::%s\n' "$*" >&2
    exit 1
}

warn()
{
    printf '::warning title=Offline hosted inputs::%s\n' "$*" >&2
}

summary()
{
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
    fi
}

# This runner as the manifest records it: the image (GitHub's ImageOS and ImageVersion), the release and dpkg's
# architecture.
runner_image()
{
    local release
    [[ "${ImageOS:-}" =~ ^[A-Za-z0-9._-]+$ && "${ImageVersion:-}" =~ ^[A-Za-z0-9._-]+$ ]] || die 'the apt bundle is its runner image'"'"'s: ImageOS and ImageVersion must name the image, as on a GitHub-hosted runner'
    release=$(. /etc/os-release && printf '%s' "$VERSION_ID")
    printf '%s %s %s %s\n' "$ImageOS" "$ImageVersion" "$release" "$(dpkg --print-architecture)"
}

# Each installed package NAME (PACKAGE or PACKAGE:ARCH) of this runner's architecture or of none, as `PACKAGE:ARCH
# VERSION`; nothing for one there is none of.
installed_record()
{
    local native
    native=$(dpkg --print-architecture)
    { dpkg-query --show --showformat='${db:Status-Status} ${Package}:${Architecture} ${Version}\n' "$@" 2> /dev/null || true; } |
        awk -v native="$native" '$1 == "installed" && ($2 ~ (":" native "$") || $2 ~ /:all$/) { print $2, $3 }'
}

# PACKAGE<TAB>VERSION<TAB>RELATIONS (its Pre-Depends and Depends) of each .deb in DIRECTORY.
bundle_relations()
{
    local file
    while IFS= read -r -d '' file; do
        dpkg-deb --show --showformat='${Package}\t${Version}\t${Pre-Depends}, ${Depends}\n' "$file"
    done < <(find "$1" -maxdepth 1 -type f -name '*.deb' -print0)
}

# Each exact relation, NAME (= VERSION), in standard input's PACKAGE<TAB>VERSION<TAB>RELATIONS lines, as
# `PACKAGE VERSION NAME VERSION`. An architecture qualifier on NAME is dropped. (mawk's regular expressions: no classes.)
exact_relations()
{
    awk -F '\t' '{
        rest = $3
        while (match(rest, /[a-z0-9][a-z0-9.+-]*(:[a-z0-9-]+)?[ \t]*\([ \t]*=[ \t]*[^) \t]+[ \t]*\)/)) {
            relation = substr(rest, RSTART, RLENGTH)
            rest = substr(rest, RSTART + RLENGTH)
            name = relation
            sub(/[: \t(].*/, "", name)
            pinned = relation
            sub(/^[^(]*\([ \t]*=[ \t]*/, "", pinned)
            sub(/[ \t]*\)$/, "", pinned)
            print $1, $2, name, pinned
        }
    }'
}

# NAME=VERSION, one per line, of each installed package that the bundle in DIRECTORY must carry and doesn't (#2974):
# one that a .deb of the bundle depends on at exactly its installed version, and one that depends on a package of the
# bundle at exactly the bundled version. Either pins the image's own package to this image's version of it. A runner
# on another image of a rollout, where that package has moved, could not install the bundle without a download unless
# the bundle carries that version too: libc6-dbg's libc6 (= 2.43-2ubuntu2.4) failed on images with 2.43-2ubuntu2.7 on
# 2026-11-18, and dropped merge-queue candidates. An exact relation that holds for neither is an alternative apt didn't
# take, and is left alone.
pinned_outside()
{
    local directory=$1 native relations package version name pinned installed
    local -A bundled=()
    native=$(dpkg --print-architecture)
    relations=$(bundle_relations "$directory")
    while IFS=$'\t' read -r package version _; do
        [ -z "$package" ] || bundled[$package]=$version
    done <<< "$relations"
    {
        exact_relations <<< "$relations" | while read -r _ _ name pinned; do
            if [ -z "${bundled[$name]:-}" ]; then
                installed=$(installed_record "$name")
                if [ -n "$installed" ] && [ "${installed#* }" = "$pinned" ]; then
                    printf '%s=%s\n' "$name" "$pinned"
                fi
            fi
        done
        dpkg-query --show --showformat='${db:Status-Status}\t${Package}\t${Architecture}\t${Version}\t${Pre-Depends}, ${Depends}\n' |
            awk -F '\t' -v native="$native" '$1 == "installed" && ($3 == native || $3 == "all") { print $2 "\t" $4 "\t" $5 }' |
            exact_relations | while read -r package version name pinned; do
                if [ -z "${bundled[$package]:-}" ] && [ "${bundled[$name]:-}" = "$pinned" ]; then
                    printf '%s=%s\n' "$package" "$version"
                fi
            done
    } | LC_ALL=C sort -u
}

keys()
{
    local runner image_os image_version list ci
    runner=$(runner_image)
    read -r image_os image_version _ <<< "$runner"
    list=$(printf '%s\n' "${packages[@]}" | sha256sum | cut -c 1-16)
    ci=$(cd -- "$here" && cat -- "${ci_scripts[@]}" | sha256sum | cut -c 1-16)
    printf 'apt-key=hosted-apt-v%s-%s-%s-%s\n' "${format##* }" "$image_os" "$image_version" "$list"
    printf 'ci-key=hosted-ci-v1-%s\n' "$ci"
}

# Checks the apt bundle in DIRECTORY. MODE `prepare` requires this runner's image; `consume` accepts another image of
# the same release and architecture, which apt_install reports. On failure, prints why and returns 1.
check_apt()
{
    local directory=$1 mode=$2 manifest="$1/MANIFEST" runner recorded kind package version digest file extra name
    local -A files=() names=() records=()
    [ -d "$directory" ] && [ ! -L "$directory" ] || { echo "there is no apt bundle at $directory"; return 1; }
    [ -f "$manifest" ] && [ ! -L "$manifest" ] || { echo 'its MANIFEST is missing'; return 1; }
    [ "$(head -n 1 "$manifest")" = "format $format" ] || { echo "its MANIFEST is not $format's"; return 1; }
    [ "$(sed -n 's/^request //p' "$manifest")" = "${packages[*]}" ] || { echo "its package list is not setup-hosted.sh's (${packages[*]})"; return 1; }
    runner=$(runner_image)
    recorded=$(sed -n 's/^runner //p' "$manifest")
    if [ "$mode" = prepare ]; then
        [ "$recorded" = "$runner" ] || { echo "it was resolved on runner image $recorded, not on this one ($runner)"; return 1; }
    else
        [ -n "$recorded" ] && [ "${recorded#* * }" = "${runner#* * }" ] || { echo "it was resolved on $recorded, not for this runner's release and architecture ($runner)"; return 1; }
    fi
    while read -r kind package version digest file extra; do
        case "$kind" in
            format|runner|request)
                continue
                ;;
            bundle)
                [[ "$package" =~ ^[a-z0-9][a-z0-9.+-]*:[a-z0-9]+$ && -n "$version" && "$digest" =~ ^[0-9a-f]{64}$ && "$file" =~ ^[A-Za-z0-9][A-Za-z0-9.+~%_:-]*\.deb$ && -z "$extra" ]] || { echo "its MANIFEST has an invalid record for ${package:-?}"; return 1; }
                [ -f "$directory/$file" ] && [ ! -L "$directory/$file" ] || { echo "$file is missing"; return 1; }
                printf '%s  %s\n' "$digest" "$directory/$file" | sha256sum --check --status || { echo "$file does not match its SHA-256 $digest"; return 1; }
                [ -z "${files[$file]:-}" ] || { echo "its MANIFEST lists $file twice"; return 1; }
                files[$file]=1
                ;;
            image)
                [[ "$package" =~ ^[a-z0-9][a-z0-9.+-]*:[a-z0-9]+$ && -n "$version" && -z "$digest" ]] || { echo "its MANIFEST has an invalid record for ${package:-?}"; return 1; }
                ;;
            *)
                echo "its MANIFEST has an unknown record ($kind)"
                return 1
                ;;
        esac
        [ -z "${records[$package]:-}" ] || { echo "its MANIFEST records $package twice"; return 1; }
        records[$package]=1
        names[${package%:*}]=1
    done < "$manifest"
    for name in "${packages[@]}"; do
        [ -n "${names[$name]:-}" ] || { echo "its MANIFEST records no version of $name"; return 1; }
    done
    # apt would see any other .deb beside them.
    while IFS= read -r -d '' file; do
        file=${file#"$directory/"}
        [ "$file" = MANIFEST ] || [ -n "${files[$file]:-}" ] || { echo "it holds $file, which its MANIFEST doesn't list"; return 1; }
    done < <(find "$directory" -mindepth 1 -print0)
    # On its own image, it carries every package of this image's that it pins exactly.
    if [ "$mode" = prepare ]; then
        extra=$(pinned_outside "$directory")
        [ -z "$extra" ] || { echo "it pins this image's $(tr '\n' ' ' <<< "$extra")without carrying them"; return 1; }
    fi
}

# A runner's resolver can fail on Ubuntu's mirror names for a long while: from 13:51 UTC on 2026-11-11, for well over
# half an hour, every hosted job failed in apt-get with "Temporary failure resolving 'archive.ubuntu.com'" (and
# 'security.ubuntu.com', 'azure.archive.ubuntu.com') while packages.microsoft.com resolved on the same runner, and some
# public resolvers (dns.google) gave no address for those names either. So before each of the preparation's network
# attempts, each apt host that doesn't resolve here is pinned in /etc/hosts to an address a DNS-over-HTTPS resolver
# gives it (Cloudflare's, else Google's), as setup-hosted.sh did before it stopped using the network. This brings in
# nothing unverified: apt checks the archive's signed Release files and every package's hash, and the downloads here
# keep their SHA-256 pins.
apt_hosts()
{
    local file
    for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/*; do
        if [ -f "$file" ]; then
            { grep -vE '^[[:space:]]*#' "$file" || true; } | { grep -oE 'https?://[^/[:space:]]+' || true; }
        fi
    done | sed -E 's#^https?://##'
    printf '%s\n' archive.ubuntu.com security.ubuntu.com azure.archive.ubuntu.com snapshot.ubuntu.com
}

resolve_over_https()
{
    local url addresses=''
    for url in "https://cloudflare-dns.com/dns-query?name=$1&type=A" "https://dns.google/resolve?name=$1&type=A"; do
        if [ -z "$addresses" ]; then
            addresses=$(curl --fail --silent --show-error --max-time 10 --retry 2 -H 'accept: application/dns-json' "$url" |
                grep -oE '"data": *"[0-9]+(\.[0-9]+){3}"' | grep -oE '[0-9]+(\.[0-9]+){3}' | head -n 1) || addresses=''
        fi
    done
    printf '%s' "$addresses"
}

pin_unresolved_apt_hosts()
{
    local host address
    for host in $(apt_hosts | sort -u); do
        if ! timeout 10 getent hosts "$host" > /dev/null; then
            address=$(resolve_over_https "$host")
            if [ -n "$address" ]; then
                printf '%s %s\n' "$address" "$host" | sudo tee -a "$hosts_file" > /dev/null
                log "$host does not resolve here; pinned it to $address through DNS over HTTPS"
            else
                log "$host does not resolve here, nor over HTTPS"
            fi
        fi
    done
}

# apt-get "$@" up to 3 times, pinning hosts again and refreshing the package lists between an install's attempts, with
# the pauses above. Each attempt is bounded, so a resolver that times out on every request can't hold the job for an
# hour: a failing attempt took about 10 minutes. apt's own Acquire::Retries retries each file too.
apt_network()
{
    local attempt=1 status=0
    pin_unresolved_apt_hosts
    sudo timeout 480 apt-get -o Acquire::Retries=3 "$@" || status=$?
    while [ "$status" -ne 0 ] && [ "$attempt" -lt 3 ]; do
        log "apt-get $1 failed with status $status (attempt $attempt of 3); retrying in $((attempt * retry_seconds)) s"
        sleep "$((attempt * retry_seconds))"
        attempt=$((attempt + 1))
        status=0
        pin_unresolved_apt_hosts
        if [ "$1" != update ]; then
            sudo timeout 480 apt-get -o Acquire::Retries=3 update || true
        fi
        sudo timeout 480 apt-get -o Acquire::Retries=3 "$@" || status=$?
    done
    [ "$status" -eq 0 ] || die "apt-get $1 failed $attempt times, the last with status $status"
}

# apt-get install of the bundle's .debs in DIRECTORY, seeing no package lists: only the installed packages and these
# files, which is all a consumer has. Extra arguments go before `install` (--simulate). ARCHIVES is apt's directory
# for the files it installs: the bundle's own for a real install, which fails otherwise, and a scratch one for a
# simulation, which leaves the bundle as it is.
offline_apt()
{
    local directory=$1 archives=$2 lists status=0
    local -a debs=()
    shift 2
    mapfile -t debs < <(awk -v directory="$directory" '$1 == "bundle" { print directory "/" $5 }' "$directory/MANIFEST")
    if [ "${#debs[@]}" -eq 0 ]; then
        log 'the runner image already has every package at the recorded version'
        return 0
    fi
    lists=$(mktemp -d)
    mkdir "$lists/partial"
    # A carried package moves to the bundle's version, down as well as up; nothing of the image's is removed for it.
    sudo apt-get "$@" install --yes --no-install-recommends --no-download --allow-downgrades --no-remove \
        -o "Dir::State::Lists=$lists" -o Dir::Cache::pkgcache= -o Dir::Cache::srcpkgcache= \
        -o "Dir::Cache::archives=$archives" "${debs[@]}" || status=$?
    # apt ran as root: it may leave root's files there.
    sudo rm -rf -- "$lists"
    return "$status"
}

write_manifest()
{
    local directory=$1 file package version arch name
    local -A bundled=()
    {
        printf 'format %s\n' "$format"
        printf 'runner %s\n' "$(runner_image)"
        printf 'request %s\n' "${packages[*]}"
        while IFS= read -r -d '' file; do
            read -r package version arch <<< "$(dpkg-deb --show --showformat='${Package} ${Version} ${Architecture}\n' "$file")"
            printf 'bundle %s:%s %s %s %s\n' "$package" "$arch" "$version" "$(sha256sum < "$file" | cut -c 1-64)" "${file##*/}"
            bundled[$package]=1
        done < <(find "$directory" -maxdepth 1 -type f -name '*.deb' -print0 | LC_ALL=C sort -z)
        for name in "${packages[@]}"; do
            if [ -z "${bundled[$name]:-}" ]; then
                package=$(installed_record "$name")
                [ -n "$package" ] || die "apt-get resolved $name to nothing, and this runner image doesn't have it"
                printf 'image %s\n' "$package"
            fi
        done
    } > "$directory/MANIFEST"
}

# Adds to the bundle in DIRECTORY each package that pinned_outside names, downloaded at that version through SCRATCH,
# until it names none: a carried package can pin others in turn.
carry_pinned()
{
    local directory=$1 scratch=$2 list entry
    local -a wanted=()
    local -A requested=()
    mkdir -p "$scratch"
    list=$(pinned_outside "$directory")
    while [ -n "$list" ]; do
        mapfile -t wanted <<< "$list"
        for entry in "${wanted[@]}"; do
            [ -z "${requested[$entry]:-}" ] || die "the closure still pins this image's $entry after it was downloaded"
            requested[$entry]=1
            log "carrying $entry: the closure pins this image's version of it exactly"
        done
        (cd "$scratch" && apt_network download "${wanted[@]}")
        sudo chown -R "$(id -u):$(id -g)" "$scratch"
        find "$scratch" -maxdepth 1 -type f -name '*.deb' -exec mv -t "$directory" -- {} +
        list=$(pinned_outside "$directory")
    done
}

prepare_apt()
{
    local root=$1 directory="$1/apt" reason count bytes
    if reason=$(check_apt "$directory" prepare); then
        log 'input=apt source=cache'
        apt_source=cache
    else
        log "input=apt source=network reason=$reason"
        apt_source=network
        # Beside ROOT, not in it: a failed attempt leaves nothing in the bundle.
        temporary=$(mktemp -d "${root%/}.apt.XXXXXXXX")
        mkdir -p "$temporary/archives/partial" "$temporary/apt" "$temporary/simulation"
        apt_network update
        apt_network install --download-only --yes --no-install-recommends -o "Dir::Cache::archives=$temporary/archives" "${packages[@]}"
        sudo chown -R "$(id -u):$(id -g)" "$temporary/archives"
        find "$temporary/archives" -maxdepth 1 -type f -name '*.deb' -exec mv -t "$temporary/apt" -- {} +
        carry_pinned "$temporary/apt" "$temporary/carried"
        write_manifest "$temporary/apt"
        # What every consumer will do, on this image: the .debs alone must install, with no download.
        offline_apt "$temporary/apt" "$temporary/simulation" --simulate || die 'the downloaded closure does not install from the bundle alone on the image it was resolved on'
        reason=$(check_apt "$temporary/apt" prepare) || die "the prepared apt bundle fails its own check: $reason"
        rm -rf -- "$directory"
        mv -- "$temporary/apt" "$directory"
        sudo rm -rf -- "$temporary"
        temporary=''
    fi
    count=$(grep -c '^bundle ' "$directory/MANIFEST" || true)
    bytes=$(du -sb -- "$directory" | cut -f 1)
    apt_detail="$count .debs, $bytes bytes, runner image $(sed -n 's/^runner //p' "$directory/MANIFEST")"
}

# Lua 5.4.8, as the existing Tier 1 runtime measurement pins it. Verification precedes extraction, including on an
# offline consumer; the archive stays in the shared input cache, while each caller gets its own extracted tree.
fetch_lua()
{
    local root=$1
    temporary=$(mktemp -d)
    bash "$here/cached-download.sh" https://www.lua.org/ftp/lua-5.4.8.tar.gz \
        4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae "$temporary/lua.tar.gz"
    mkdir -p -- "$root"
    tar --extract --gzip --file "$temporary/lua.tar.gz" --directory "$root" --strip-components=1 --no-same-owner
    [ -f "$root/src/lua.c" ] && [ -f "$root/src/luac.c" ] || die 'the Lua archive lacks the interpreter or compiler source'
}

# Runs input NAME's own script with BUSTER_CI_CACHE=CACHE into SCRATCH: offline (1) or allowed to fetch (0).
run_ci_input()
{
    local name=$1 offline=$2 cache=$3 scratch=$4
    case "$name" in
        corpus)
            BUSTER_CI_CACHE=$cache BUSTER_CI_OFFLINE=$offline bash "$here/fetch-corpus.sh" "$scratch/corpus" ;;
        c-testsuite)
            BUSTER_CI_CACHE=$cache BUSTER_CI_OFFLINE=$offline bash "$here/fetch-c-testsuite.sh" "$scratch/c-testsuite" ;;
        sqlite)
            BUSTER_CI_CACHE=$cache BUSTER_CI_OFFLINE=$offline bash "$here/fetch-sqlite.sh" "$scratch/sqlite" ;;
        lua)
            BUSTER_CI_CACHE=$cache BUSTER_CI_OFFLINE=$offline bash "$here/hosted-offline.sh" lua "$scratch/lua" ;;
    esac > "$scratch.log" 2>&1
}

# Each input as a consumer reads it, offline; only an input that fails so is acquired, by the same script allowed to
# fetch, up to 3 times. cached-download.sh retries each download for 240 s too.
prepare_ci()
{
    local cache="$1/ci" name attempt reason
    mkdir -p "$cache"
    for name in "${ci_inputs[@]}"; do
        temporary=$(mktemp -d)
        if run_ci_input "$name" 1 "$cache" "$temporary/run"; then
            log "input=$name source=cache"
            ci_sources[$name]=cache
        else
            reason=$(tail -n 1 "$temporary/run.log")
            log "input=$name source=network reason=$reason"
            ci_sources[$name]=network
            attempt=1
            pin_unresolved_apt_hosts
            until rm -rf -- "$temporary/run" && run_ci_input "$name" 0 "$cache" "$temporary/run"; do
                sed 's/^/    /' "$temporary/run.log" >&2
                [ "$attempt" -lt 3 ] || die "acquiring $name failed 3 times"
                log "acquiring $name failed (attempt $attempt of 3); retrying in $((attempt * retry_seconds)) s"
                sleep "$((attempt * retry_seconds))"
                attempt=$((attempt + 1))
                pin_unresolved_apt_hosts
            done
        fi
        rm -rf -- "$temporary"
        temporary=''
    done
    # The scripts' lock files are theirs to create; the bundle holds the inputs alone.
    find "$cache" -maxdepth 2 -type f -name '*.lock' -delete
}

prepare()
{
    local root=$1 start=$SECONDS name
    apt_source='' apt_detail=''
    declare -gA ci_sources=()
    mkdir -p -- "$root"
    prepare_apt "$root"
    prepare_ci "$root"
    log "prepared=$root seconds=$((SECONDS - start)) bytes=$(du -sb -- "$root" | cut -f 1)"
    summary '### Offline inputs'
    summary ''
    summary 'Every hosted job installs and reads these from this bundle, with no network (tools/ci/hosted-offline.sh).'
    summary ''
    summary '| Input | Source |'
    summary '| --- | --- |'
    summary "| apt ($apt_detail) | $apt_source |"
    for name in "${ci_inputs[@]}"; do
        summary "| $name | ${ci_sources[$name]} |"
    done
    summary ''
}

verify()
{
    local root=$1 reason
    reason=$(check_apt "$root/apt" consume) || die "the offline bundle at $root can't be used: $reason. Nothing is fetched in its place: rerun the workflow, whose preparation job acquires the inputs"
    [ -d "$root/ci" ] && [ ! -L "$root/ci" ] || die "the offline bundle at $root has no ci directory"
    log "verified $root: $(grep -c '^bundle ' "$root/apt/MANIFEST" || true) .debs; prepared on runner image $(sed -n 's/^runner //p' "$root/apt/MANIFEST")"
}

apt_install()
{
    local root=$1 directory="$1/apt" runner recorded same=true kind package version actual origin differences=0
    local -a names=()
    local -A before=()
    verify "$root"
    runner=$(runner_image)
    recorded=$(sed -n 's/^runner //p' "$directory/MANIFEST")
    if [ "$recorded" != "$runner" ]; then
        same=false
        warn "this runner's image ($runner) is not the one the apt bundle was resolved on ($recorded), as during an image rollout: its .debs are installed all the same, and the image's own packages are compared below"
    fi
    # What the image had of the bundle's packages: a carried one, or one the bundle upgrades.
    mapfile -t names < <(awk '$1 == "bundle" { print $2 }' "$directory/MANIFEST")
    if [ "${#names[@]}" -ne 0 ]; then
        while read -r package version; do
            before[$package]=$version
        done < <(installed_record "${names[@]}")
    fi
    if ! offline_apt "$directory" "$directory"; then
        if [ "$same" = true ]; then
            die 'the apt bundle'"'"'s .debs do not install on the image they were resolved on without a download. Nothing is fetched in their place'
        fi
        die "the apt bundle's .debs, resolved on runner image $recorded, do not install on this one ($runner) without a download. Nothing is fetched in their place: rerun the failed jobs, which may draw the bundle's image"
    fi
    summary '### Packages installed offline'
    summary ''
    summary "The apt bundle was resolved on \`$recorded\`; this runner is \`$runner\`."
    summary ''
    summary '| Package | Version | From |'
    summary '| --- | --- | --- |'
    while read -r kind package version _; do
        case "$kind" in
            bundle) origin='the bundle' ;;
            image) origin='the runner image' ;;
            *) continue ;;
        esac
        actual=$(installed_record "$package")
        actual=${actual#* }
        if [ "$actual" != "$version" ]; then
            if [ "$kind" = bundle ] || [ "$same" = true ]; then
                die "$package is installed at ${actual:-no version}, not at the recorded $version"
            fi
            warn "$package is the runner image's ${actual:-(none)}, not the recorded $version"
            differences=$((differences + 1))
            origin="$origin (recorded $version)"
        elif [ "$kind" = bundle ] && [ -n "${before[$package]:-}" ] && [ "${before[$package]}" != "$version" ]; then
            origin="$origin (the image had ${before[$package]})"
        fi
        printf 'hosted-offline: %s %s from %s\n' "$package" "${actual:-(none)}" "$origin"
        summary "| \`$package\` | \`${actual:-(none)}\` | $origin |"
    done < "$directory/MANIFEST"
    summary ''
    if [ "$differences" -ne 0 ]; then
        summary "$differences of the runner image's own packages differ from the recorded versions."
        summary ''
    fi
}

command=${1:-}
case "$command" in
    lua)
        [ $# -eq 2 ] && [ -n "$2" ] && [ "$2" != / ] || die 'usage: hosted-offline.sh lua DIRECTORY'
        fetch_lua "$2"
        ;;
    keys)
        [ $# -eq 1 ] || die 'usage: hosted-offline.sh keys'
        runner_image > /dev/null
        keys
        ;;
    prepare|verify|apt-install)
        [ $# -eq 2 ] && [ -n "$2" ] && [ "$2" != / ] || die "usage: hosted-offline.sh $command ROOT"
        runner_image > /dev/null
        case "$command" in
            prepare) prepare "$2" ;;
            verify) verify "$2" ;;
            apt-install) apt_install "$2" ;;
        esac
        ;;
    *)
        die 'usage: hosted-offline.sh keys | prepare ROOT | verify ROOT | apt-install ROOT | lua DIRECTORY'
        ;;
esac
