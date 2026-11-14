#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# CI's toolchain for builds outside CI (#2628): the official LLVM release, CMake and Ninja that hosted-tools.lock pins,
# which build.sh puts first on PATH unless BUSTER_TOOLCHAIN=path (docs/commands/build.md, "The toolchain").
#
#   session-toolchain.sh identity   the first line of the release's `clang --version`
#   session-toolchain.sh stage      the directories to put first on PATH, ':'-separated, staged first if they aren't
#   session-toolchain.sh layout     the hosted layout's root, its marker's line and its directories, a line each,
#                                   staged or not (build_regression.c builds a stand-in from them)
#
# stage takes the first of these, under ${XDG_CACHE_HOME:-$HOME/.cache}/buster:
#   - benchpress-clang/DIGEST, the 9700X job's own tree (setup-benchpress.sh), complete, with CMake and Ninja from
#     build-tools/ (setup-build-tools.sh): what the 9700X job builds with, the host's mold and wild among them;
#   - toolchain/KEY, the hosted rows' layout (install-hosted-tools.sh: clang and LLVM's tools, CMake, Ninja, mold and
#     wild), staged once from the verified release inputs (hosted-release.sh prepare: its cache, the pinned GHCR
#     mirror, then upstream, each file checked against the lock's SHA-256), about 1.3 GB to download, and kept; the
#     downloaded inputs are removed once installed. KEY names the lock's contents, so a new pin stages afresh.
# A tree is used by its marker, written once it was verified and installed, with no network; staging needs curl,
# zstd, tar, sha256sum and flock, and the network once.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
lock="$here/hosted-tools.lock"
die()
{
    printf 'session-toolchain.sh: %s\n' "$*" >&2
    exit 1
}
# The lock's versions and digests in one read, and the release commit, the license's (setup-benchpress.sh), whose hash
# the release's clang reports.
read -r version digest commit cmake_version cmake_digest ninja_version ninja_digest < <(awk '
    $1 == "llvm" { version = $2; digest = $3 }
    $1 == "llvm-license" && match($4, /llvm-project\/[0-9a-f]+\//) { commit = substr($4, RSTART + 13, RLENGTH - 14) }
    $1 == "cmake" { cmake_version = $2; cmake_digest = $3 }
    $1 == "ninja" { ninja_version = $2; ninja_digest = $3 }
    END { print version, digest, commit, cmake_version, cmake_digest, ninja_version, ninja_digest }' "$lock") || true
[[ "$version" =~ ^[0-9.]+$ && "$commit" =~ ^[0-9a-f]{40}$ && -n "$ninja_digest" ]] || die "$lock names no LLVM version, release commit, CMake or Ninja"
identity="clang version $version (https://github.com/llvm/llvm-project $commit)"
# The first line of $1's --version, or nothing.
first_line()
{
    local text
    text=$("$1" --version 2> /dev/null) || return 0
    printf '%s\n' "${text%%$'\n'*}"
}
# Whether the file $1 holds the line $2 alone.
marked()
{
    [ -f "$1" ] && [ "$(< "$1")" = "$2" ]
}
cache="${XDG_CACHE_HOME:-$HOME/.cache}/buster"
tools_name="cmake-$cmake_version-ninja-$ninja_version"
tools_marker="cmake $cmake_version $cmake_digest, ninja $ninja_version $ninja_digest"
# The hosted layout: KEY names the lock's contents but its mirror's, as hosted-release.sh's publication tag does.
key=$(grep -v '^mirror ' "$lock" | sha256sum | cut -c 1-16)
root="$cache/toolchain/$key"
directories="$root/build-tools/$tools_name/bin:$root/llvm-$version/bin:$root/bin"
usage='usage: session-toolchain.sh identity | stage | layout'
case "${1:-}" in
    identity)
        [ $# -eq 1 ] || die "$usage"
        printf '%s\n' "$identity"
        ;;
    layout)
        [ $# -eq 1 ] || die "$usage"
        printf '%s\n' "$root" "$key" "$directories"
        ;;
    stage)
        [ $# -eq 1 ] || die "$usage"
        benchpress="$cache/benchpress-clang/$digest"
        if marked "$benchpress/.manifest-complete" "$digest" && [ "$(first_line "$benchpress/bin/clang")" = "$identity" ]; then
            tools="$cache/build-tools/$tools_name/bin"
            if ! marked "$cache/build-tools/$tools_name/.complete" "$tools_marker"; then
                tools=$(bash "$here/setup-build-tools.sh" "$cache/build-tools")
            fi
            printf '%s:%s\n' "$tools" "$benchpress/bin"
        else
            if ! marked "$root/.complete" "$key"; then
                for tool in curl zstd tar sha256sum flock; do
                    command -v "$tool" > /dev/null || die "staging CI's toolchain needs $tool on PATH (or build with BUSTER_TOOLCHAIN=path)"
                done
                mkdir -p -- "$cache/toolchain"
                exec 9> "$root.lock"
                flock 9
                if ! marked "$root/.complete" "$key"; then
                    printf "session-toolchain.sh: staging CI's toolchain (LLVM %s, CMake %s, Ninja %s, mold, wild; about 1.3 GB to download) in %s, once\n" \
                        "$version" "$cmake_version" "$ninja_version" "$root" >&2
                    # An earlier attempt's partial tree goes; its verified inputs, if any, are kept for prepare to reuse.
                    rm -rf -- "$root"
                    bash "$here/hosted-release.sh" prepare "$root.inputs" >&2 || die "couldn't acquire the release inputs (or build with BUSTER_TOOLCHAIN=path)"
                    BUSTER_HOSTED_RELEASES="$root.inputs" bash "$here/install-hosted-tools.sh" "$root" >&2 || die "couldn't install the release in $root"
                    [ "$(first_line "$root/llvm-$version/bin/clang")" = "$identity" ] || die "$root's clang isn't the release's"
                    printf '%s\n' "$key" > "$root/.complete"
                    rm -rf -- "$root.inputs" "$root.inputs.lock"
                    printf 'session-toolchain.sh: staged in %s\n' "$root" >&2
                fi
            fi
            printf '%s\n' "$directories"
        fi
        ;;
    *)
        die "$usage"
        ;;
esac
