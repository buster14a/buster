#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Network-free fixtures for the hosted jobs' offline inputs (#2275): tools/ci/hosted-offline.sh's preparation, handoff
# check and offline apt install, and the offline modes of cached-download.sh, fetch-c-testsuite.sh, fetch-sqlite.sh and
# hosted-offline.sh lua.
# They cover a cold preparation (a cache miss, through retried failures), a warm one (a hit, with every source gone), a
# consumer through an artifact-shaped copy, a missing file, a bad hash, a changed runner image and a changed package
# list. apt-get, dpkg-query, sudo and curl are stand-ins; the .debs are real ones that dpkg-deb builds and reads, and
# the mirrors are real Git repositories behind file:// upstreams.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail()
{
    printf 'hosted-offline fixture: %s\n' "$*" >&2
    exit 1
}
mkdir -p "$work/tools/ci" "$work/bin" "$work/upstream" "$work/repository" "$work/sources"
cp "$root/tools/ci/hosted-offline.sh" "$root/tools/ci/cached-download.sh" "$root/tools/ci/fetch-c-testsuite.sh" \
    "$root/tools/ci/fetch-sqlite.sh" "$work/tools/ci/"
offline="$work/tools/ci/hosted-offline.sh"
packages=$(sed -n 's/^packages=(\(.*\))$/\1/p' "$offline")
[ -n "$packages" ] || fail 'hosted-offline.sh has no package list'

# The pinned downloads: stand-ins for setup-baseline-runtime.sh's and fetch-corpus.sh's, which read theirs through the
# real cached-download.sh from https URLs that the stand-in curl serves.
printf 'libc runtime\n' > "$work/upstream/runtime.deb"
printf 'gcc runtime\n' > "$work/upstream/gcc.deb"
mkdir "$work/corpus-tree"
printf 'int x;\n' > "$work/corpus-tree/a.c"
tar -cf "$work/upstream/corpus.tar" -C "$work/corpus-tree" .
digest()
{
    sha256sum "$1" | cut -c 1-64
}
# The real Lua subcommand and its extraction, with only its pinned URL and digest replaced in the copied script.
mkdir -p "$work/lua-tree/lua-5.4.8/src"
printf 'int lua_fixture;\n' > "$work/lua-tree/lua-5.4.8/src/lua.c"
printf 'int luac_fixture;\n' > "$work/lua-tree/lua-5.4.8/src/luac.c"
tar -czf "$work/upstream/lua-5.4.8.tar.gz" -C "$work/lua-tree" lua-5.4.8
lua_digest=$(digest "$work/upstream/lua-5.4.8.tar.gz")
sed -i "s|https://www.lua.org/ftp/lua-5.4.8.tar.gz|https://fixture.invalid/lua-5.4.8.tar.gz|; s/4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae/$lua_digest/" "$offline"
cat > "$work/tools/ci/setup-baseline-runtime.sh" << SCRIPT
#!/usr/bin/env bash
set -euo pipefail
mkdir -p "\$RUNNER_TEMP/runtime"
bash "\$(dirname "\$0")/cached-download.sh" https://fixture.invalid/runtime.deb $(digest "$work/upstream/runtime.deb") "\$RUNNER_TEMP/runtime/runtime.deb"
bash "\$(dirname "\$0")/cached-download.sh" https://fixture.invalid/gcc.deb $(digest "$work/upstream/gcc.deb") "\$RUNNER_TEMP/runtime/gcc.deb"
SCRIPT
cat > "$work/tools/ci/fetch-corpus.sh" << SCRIPT
#!/usr/bin/env bash
set -euo pipefail
archive=\$(mktemp)
trap 'rm -f "\$archive"' EXIT
bash "\$(dirname "\$0")/cached-download.sh" https://fixture.invalid/corpus.tar $(digest "$work/upstream/corpus.tar") "\$archive"
mkdir -p "\$1"
tar -xf "\$archive" -C "\$1"
test -f "\$1/a.c"
SCRIPT
corpus_digest=$(digest "$work/upstream/corpus.tar")

# c-testsuite's and SQLite's upstreams, at their pinned commits.
git_upstream()
{
    local directory=$1
    git init -q -b main "$directory"
    printf '%s\n' "${@:2}" > "$directory/VERSION"
    printf 'int main(void) { return 0; }\n' > "$directory/00001.c"
    git -C "$directory" add -A
    git -C "$directory" -c user.name=t -c user.email=t@example.com commit -q -m tree
    git -C "$directory" config uploadpack.allowAnySHA1InWant true
}
git_upstream "$work/sources/c-testsuite" suite
git_upstream "$work/sources/sqlite" 3.53.2
export BUSTER_C_TESTSUITE_URL="file://$work/sources/c-testsuite" BUSTER_C_TESTSUITE_COMMIT=$(git -C "$work/sources/c-testsuite" rev-parse HEAD)
export BUSTER_SQLITE_URL="file://$work/sources/sqlite" BUSTER_SQLITE_COMMIT=$(git -C "$work/sources/sqlite" rev-parse HEAD)

# The runner image's packages (FIXTURE/status: NAME ARCH VERSION) and the archive's .debs, real ones. The image has the
# list's toolchain packages, an older zstd, and libimage, which tcl8.6 needs but the bundle doesn't hold.
make_deb()
{
    local name=$1 version=$2 architecture=$3 depends=${4:-} directory="$work/debs/$1"
    mkdir -p "$directory/DEBIAN"
    {
        printf 'Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: fixture <fixture@example.com>\n' "$name" "$version" "$architecture"
        [ -z "$depends" ] || printf 'Depends: %s\n' "$depends"
        printf 'Description: fixture\n'
    } > "$directory/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$directory" "$work/repository/${name}_${version}_${architecture}.deb" > /dev/null
}
native=$(dpkg --print-architecture)
image_status()
{
    local name
    for name in gcc g++ binutils libc6-dev libzstd-dev libxml2-dev zlib1g-dev; do
        printf '%s %s 1.0\n' "$name" "$native"
    done
    printf 'zstd %s 1.0\nlibimage %s 1.0\n' "$native" "$native"
}
make_deb gdb 2.0 "$native" 'libgdbdep (>= 2.0), gdb-common (= 2.0)'
make_deb libgdbdep 2.0 "$native"
make_deb gdb-common 2.0 all
make_deb tcc 2.0 "$native"
make_deb linux-perf 2.0 "$native"
make_deb qemu-user 1:2.0 "$native"
make_deb valgrind 2.0 "$native"
make_deb tcl8.6 2.0 "$native" 'libimage'
make_deb tcl8.6-dev 2.0 "$native" 'tcl8.6 (= 2.0)'
make_deb zstd 1.1 "$native"

# Stand-ins: sudo runs its command; getent resolves every name but FIXTURE/unresolvable's, unless pinned; curl answers DNS over
# HTTPS with 192.0.2.7, serves FIXTURE/upstream and fails FIXTURE/fail-curl times first; dpkg-query reads
# FIXTURE/status; apt-get updates (failing FIXTURE/fail-update times first), downloads a closure from the archive, or
# installs .debs given by path, checking their dependencies against the status and each other. Every request is
# logged.
cat > "$work/bin/sudo" << 'SH'
#!/usr/bin/env bash
exec "$@"
SH
cat > "$work/bin/getent" << 'SH'
#!/usr/bin/env bash
# A pinned name resolves, as /etc/hosts makes it.
grep -q " ${!#}\$" "$BUSTER_HOSTED_HOSTS_FILE" 2> /dev/null || ! grep -qxF "${!#}" "$FIXTURE/unresolvable" 2> /dev/null
SH
cat > "$work/bin/curl" << 'SH'
#!/usr/bin/env bash
set -euo pipefail
output='' url=''
while [ $# -gt 0 ]; do
    case "$1" in
        --output) output=$2; shift ;;
        https://*|http://*) url=$1 ;;
    esac
    shift
done
printf '%s\n' "$url" >> "$FIXTURE/curl-requests"
if [[ "$url" = https://cloudflare-dns.com/dns-query\?* ]]; then
    printf '{"Status":0,"Answer":[{"name":"x","type":1,"data": "192.0.2.7"}]}\n'
    exit 0
fi
count=$(cat "$FIXTURE/fail-curl" 2> /dev/null || echo 0)
if [ "$count" -gt 0 ]; then
    echo $((count - 1)) > "$FIXTURE/fail-curl"
    exit 7
fi
[ -f "$FIXTURE/upstream/${url##*/}" ] || exit 22
cp "$FIXTURE/upstream/${url##*/}" "$output"
SH
cat > "$work/bin/dpkg-query" << 'SH'
#!/usr/bin/env bash
set -euo pipefail
name=${!#}
found=$(awk -v name="${name%%:*}" -v arch="${name#*:}" -v qualified="$([[ "$name" == *:* ]] && echo 1 || echo 0)" \
    '$1 == name && (!qualified || $2 == arch) { printf "installed %s:%s %s\n", $1, $2, $3 }' "$FIXTURE/status")
[ -n "$found" ] || { echo "dpkg-query: no packages found matching $name" >&2; exit 1; }
printf '%s\n' "$found"
SH
cat > "$work/bin/apt-get" << 'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$FIXTURE/apt-requests"
command='' simulate=false download_only=false no_download=false archives='' lists='' arguments=()
while [ $# -gt 0 ]; do
    case "$1" in
        -o) case "$2" in Dir::Cache::archives=*) archives=${2#*=} ;; Dir::State::Lists=*) lists=${2#*=} ;; esac; shift ;;
        --simulate) simulate=true ;;
        --download-only) download_only=true ;;
        --no-download) no_download=true ;;
        --yes|--no-install-recommends) ;;
        update|install) command=$1 ;;
        -*) echo "apt-get stand-in: unexpected option $1" >&2; exit 100 ;;
        *) arguments+=("$1") ;;
    esac
    shift
done
status_version()
{
    awk -v name="$1" '$1 == name { print $3 }' "$FIXTURE/status"
}
fail_count()
{
    local count
    count=$(cat "$FIXTURE/$1" 2> /dev/null || echo 0)
    if [ "$count" -gt 0 ]; then
        echo $((count - 1)) > "$FIXTURE/$1"
        echo "apt-get stand-in: Temporary failure resolving 'archive.ubuntu.com'" >&2
        exit 100
    fi
}
if [ "$command" = update ]; then
    fail_count fail-update
    exit 0
fi
[ "$command" = install ] || { echo 'apt-get stand-in: no command' >&2; exit 100; }
if [ "$download_only" = true ]; then
    fail_count fail-download
    [ -d "$archives/partial" ] || { echo 'apt-get stand-in: no partial directory' >&2; exit 100; }
    # The closure: each requested package, and its dependencies, that the image lacks or has older.
    declare -A seen=()
    queue=("${arguments[@]}")
    while [ "${#queue[@]}" -gt 0 ]; do
        name=${queue[0]}
        queue=("${queue[@]:1}")
        [ -z "${seen[$name]:-}" ] || continue
        seen[$name]=1
        deb=$(find "$FIXTURE/repository" -name "${name}_*.deb" -print -quit)
        installed=$(status_version "$name")
        if [ -z "$deb" ]; then
            [ -n "$installed" ] || { echo "apt-get stand-in: unable to locate $name" >&2; exit 100; }
            continue
        fi
        version=$(dpkg-deb --show --showformat='${Version}' "$deb")
        if [ -n "$installed" ] && dpkg --compare-versions "$installed" ge "$version"; then
            continue
        fi
        cp "$deb" "$archives/"
        for dependency in $(dpkg-deb --show --showformat='${Depends}' "$deb" | tr ',' '\n' | sed 's/(.*)//; s/[[:space:]]//g'); do
            queue+=("$dependency")
        done
    done
    exit 0
fi
# An install of .debs by path: no download, no package lists, and every dependency installed or among them.
[ "$no_download" = true ] || { echo 'apt-get stand-in: a consumer install must not download' >&2; exit 100; }
[ -d "$lists" ] && [ -z "$(find "$lists" -mindepth 1 ! -name partial -print -quit)" ] || { echo 'apt-get stand-in: package lists in use' >&2; exit 100; }
declare -A versions=()
for deb in "${arguments[@]}"; do
    [[ "$deb" = /* && -f "$deb" ]] || { echo "apt-get stand-in: no such file $deb" >&2; exit 100; }
    [ "$(dirname "$deb")" = "$archives" ] || [ "$simulate" = true ] || { echo "E: Internal Error, Pathname to install is not absolute" >&2; exit 100; }
    versions[$(dpkg-deb --show --showformat='${Package}' "$deb")]=$(dpkg-deb --show --showformat='${Version}' "$deb")
done
for deb in "${arguments[@]}"; do
    while read -r dependency; do
        [ -n "$dependency" ] || continue
        name=${dependency%%(*}
        name=${name//[[:space:]]/}
        have=${versions[$name]:-$(status_version "$name")}
        [ -n "$have" ] || { echo "apt-get stand-in: $(basename "$deb") depends on $name, which is not installable" >&2; exit 100; }
        if [[ "$dependency" =~ \((=|\>=)[[:space:]]*([^\)]*)\) ]]; then
            operator=${BASH_REMATCH[1]}
            [ "$operator" = '=' ] && operator=eq || operator=ge
            dpkg --compare-versions "$have" "$operator" "${BASH_REMATCH[2]}" || { echo "apt-get stand-in: unmet $dependency" >&2; exit 100; }
        fi
    done < <(dpkg-deb --show --showformat='${Depends}\n' "$deb" | tr ',' '\n')
done
[ "$simulate" = false ] || exit 0
for deb in "${arguments[@]}"; do
    read -r name version architecture <<< "$(dpkg-deb --show --showformat='${Package} ${Version} ${Architecture}' "$deb")"
    awk -v name="$name" '$1 != name' "$FIXTURE/status" > "$FIXTURE/status.new"
    printf '%s %s %s\n' "$name" "$architecture" "$version" >> "$FIXTURE/status.new"
    mv "$FIXTURE/status.new" "$FIXTURE/status"
done
mkdir -p "$archives/partial"
: > "$archives/lock"
SH
chmod +x "$work/bin/"*
export FIXTURE="$work" PATH="$work/bin:$PATH" BUSTER_HOSTED_RETRY_SECONDS=0 ImageOS=ubuntu26 ImageVersion=20261101.1
export BUSTER_HOSTED_HOSTS_FILE="$work/hosts"
unset BUSTER_CI_CACHE BUSTER_CI_OFFLINE GITHUB_STEP_SUMMARY GITHUB_ENV
image_status > "$work/status"
bundle="$work/bundle"
requests()
{
    cat "$work/$1" 2> /dev/null | grep -c . || true
}
reset_requests()
{
    rm -f "$work/apt-requests" "$work/curl-requests"
}
hide_sources()
{
    mv "$work/upstream" "$work/upstream.hidden"
    mv "$work/sources" "$work/sources.hidden"
}
restore_sources()
{
    mv "$work/upstream.hidden" "$work/upstream"
    mv "$work/sources.hidden" "$work/sources"
}

# The keys: apt's names the package list and the image, ci's the pinning scripts, including Lua's owner.
keys=$(bash "$offline" keys)
grep -qx 'apt-key=hosted-apt-v1-ubuntu26-20261101.1-[0-9a-f]\{16\}' <<< "$keys" || fail "unexpected apt key: $keys"
grep -qx 'ci-key=hosted-ci-v1-[0-9a-f]\{16\}' <<< "$keys" || fail "unexpected ci key: $keys"
[ "$(ImageVersion=20261108.2 bash "$offline" keys | grep '^ci-key=')" = "$(grep '^ci-key=' <<< "$keys")" ] || fail 'the image changed the ci key'
[ "$(ImageVersion=20261108.2 bash "$offline" keys | grep '^apt-key=')" != "$(grep '^apt-key=' <<< "$keys")" ] || fail 'the image did not change the apt key'
if ImageVersion='' bash "$offline" keys > /dev/null 2>&1; then fail 'keys without the runner image'; fi
if ImageOS='a b' bash "$offline" keys > /dev/null 2>&1; then fail 'keys with an unsafe image name'; fi
cp "$offline" "$work/offline-pinned.sh"
sed -i 's|https://fixture.invalid/lua-5.4.8.tar.gz|https://fixture.invalid/changed-lua.tar.gz|' "$offline"
[ "$(bash "$offline" keys | grep '^ci-key=')" != "$(grep '^ci-key=' <<< "$keys")" ] || fail 'the Lua pin did not change the ci key'
cp "$work/offline-pinned.sh" "$offline"

# A cache miss: every input acquired once, through two failed package-list updates and a failed download.
echo 2 > "$work/fail-update"
echo 1 > "$work/fail-curl"
reset_requests
bash "$offline" prepare "$bundle" 2> "$work/log" || { cat "$work/log" >&2; fail 'a cold preparation failed'; }
for input in apt baseline corpus c-testsuite sqlite lua; do
    grep -q "input=$input source=network" "$work/log" || fail "$input was not acquired"
done
grep -q 'apt-get update failed with status 100 (attempt 2 of 3)' "$work/log" || fail 'the update was not retried'
test ! -e "$work/hosts" || fail 'a name that resolves was pinned'
grep -q 'acquiring baseline failed (attempt 1 of 3)' "$work/log" || fail 'the download was not retried'
[ "$(grep -c '^-o Acquire::Retries=3 update$' "$work/apt-requests")" = 3 ] || fail 'the lists were not updated three times'
[ "$(grep -c -- '--download-only' "$work/apt-requests")" = 1 ] || fail 'the closure was not downloaded once'
[ "$(grep -c -- '--simulate' "$work/apt-requests")" = 1 ] || fail 'the closure was not checked offline'
[ "$(sort "$work/curl-requests" | uniq -d | grep -c .)" = 1 ] || fail 'a download other than the retried one was requested twice'
manifest="$bundle/apt/MANIFEST"
[ "$(head -n 1 "$manifest")" = 'format buster-hosted-apt 1' ] || fail 'the manifest has no format'
grep -qx "runner ubuntu26 20261101.1 .* $native" "$manifest" || fail 'the manifest does not record the runner'
grep -qx "request $packages" "$manifest" || fail 'the manifest does not record the package list'
for name in gdb libgdbdep tcc linux-perf qemu-user valgrind tcl8.6 tcl8.6-dev zstd; do
    grep -q "^bundle $name:$native " "$manifest" || fail "the bundle lacks $name"
done
grep -q '^bundle gdb-common:all 2.0 [0-9a-f]\{64\} gdb-common_2.0_all.deb$' "$manifest" || fail 'the bundle lacks gdb-common'
grep -qx "bundle qemu-user:$native 1:2.0 [0-9a-f]\{64\} qemu-user_1:2.0_$native.deb" "$manifest" || fail 'qemu-user is not recorded as dpkg-deb reads it'
for name in gcc g++ binutils libc6-dev libzstd-dev libxml2-dev zlib1g-dev; do
    grep -qx "image $name:$native 1.0" "$manifest" || fail "the image's $name is not recorded"
done
! grep -q 'libimage' "$manifest" || fail 'a dependency the image satisfies was recorded'
[ -z "$(find "$bundle/ci" -name '*.lock' -print -quit)" ] || fail 'lock files were published'
[ -z "$(find "$work" -maxdepth 1 -name 'bundle.apt.*' -print -quit)" ] || fail 'the apt staging directory survived'
test -f "$bundle/ci/downloads/$corpus_digest" || fail 'the corpus is not in the bundle'
test -f "$bundle/ci/downloads/$lua_digest" || fail 'Lua is not in the bundle'
git -C "$bundle/ci/c-testsuite.git" cat-file -e "refs/pinned/$BUSTER_C_TESTSUITE_COMMIT^{commit}" || fail 'the c-testsuite mirror lacks its commit'
git -C "$bundle/ci/sqlite.git" cat-file -e "refs/pinned/$BUSTER_SQLITE_COMMIT^{commit}" || fail 'the SQLite mirror lacks its commit'

# A cache hit: with every source gone, nothing is requested and every input is the cache's.
hide_sources
reset_requests
bash "$offline" prepare "$bundle" 2> "$work/log" || { cat "$work/log" >&2; fail 'a warm preparation failed'; }
for input in apt baseline corpus c-testsuite sqlite lua; do
    grep -q "input=$input source=cache" "$work/log" || fail "$input did not come from the cache"
done
[ "$(requests apt-requests)" = 0 ] && [ "$(requests curl-requests)" = 0 ] || fail 'a warm preparation made a request'
cp -a "$bundle" "$work/prepared"

# A consumer, after the artifact's round trip (regular files alone, empty directories and modes lost), on the image the
# bundle was resolved on: the .debs install with no download and no lists, and every input reads offline.
consume()
{
    rm -rf -- "$work/consumer"
    (cd "$work/prepared" && find . -type f -print0) | while IFS= read -r -d '' file; do
        mkdir -p "$work/consumer/$(dirname "$file")"
        cp "$work/prepared/$file" "$work/consumer/$file"
        chmod 644 "$work/consumer/$file"
    done
}
offline_env()
{
    BUSTER_CI_CACHE="$work/consumer/ci" BUSTER_CI_OFFLINE=1 "$@"
}
read_inputs()
{
    rm -rf -- "$work/read"
    mkdir "$work/read"
    offline_env env RUNNER_TEMP="$work/read" bash "$work/tools/ci/setup-baseline-runtime.sh" > /dev/null &&
        offline_env bash "$work/tools/ci/fetch-corpus.sh" "$work/read/corpus" > /dev/null &&
        offline_env bash "$work/tools/ci/fetch-c-testsuite.sh" "$work/read/c-testsuite" &&
        offline_env bash "$work/tools/ci/fetch-sqlite.sh" "$work/read/sqlite" &&
        offline_env bash "$offline" lua "$work/read/lua"
}
consume
reset_requests
export GITHUB_STEP_SUMMARY="$work/summary"
bash "$offline" verify "$work/consumer" 2> "$work/log" || { cat "$work/log" >&2; fail 'a consumer rejected the handoff'; }
bash "$offline" apt-install "$work/consumer" > "$work/install.log" 2> "$work/log" || { cat "$work/log" >&2; fail 'the offline install failed'; }
! grep -q '::warning' "$work/log" || fail 'the bundle'"'"'s own image was warned about'
[ "$(requests apt-requests)" = 1 ] && grep -q -- '--no-download' "$work/apt-requests" || fail 'the consumer did more than one offline install'
for name in gdb libgdbdep gdb-common tcc linux-perf valgrind tcl8.6 tcl8.6-dev; do
    grep -q "^$name [a-z0-9]* 2.0$" "$work/status" || fail "$name was not installed"
done
grep -qx "qemu-user $native 1:2.0" "$work/status" || fail 'qemu-user was not installed'
grep -qx "zstd $native 1.1" "$work/status" || fail 'zstd was not upgraded'
grep -qx "| \`tcl8.6-dev:$native\` | \`2.0\` | the bundle |" "$work/summary" || fail 'the summary does not list the bundle'"'"'s versions'
grep -qx "| \`gcc:$native\` | \`1.0\` | the runner image |" "$work/summary" || fail 'the summary does not list the image'"'"'s versions'
read_inputs || fail 'a consumer could not read its inputs offline'
test -f "$work/read/corpus/a.c" && test -f "$work/read/c-testsuite/00001.c" && test -f "$work/read/sqlite/VERSION" || fail 'an input was not read'
test -f "$work/read/lua/src/lua.c" && test -f "$work/read/lua/src/luac.c" || fail 'the Lua tree was not extracted'
[ "$(requests curl-requests)" = 0 ] || fail 'a consumer downloaded'
unset GITHUB_STEP_SUMMARY

# A missing file: the handoff is rejected before anything is installed, and nothing is fetched in its place.
consume
image_status > "$work/status"
reset_requests
rm "$work/consumer/apt/tcc_2.0_$native.deb"
if bash "$offline" apt-install "$work/consumer" > /dev/null 2> "$work/log"; then fail 'a missing .deb was installed around'; fi
grep -q "tcc_2.0_$native.deb is missing" "$work/log" || fail 'the missing .deb was not named'
[ "$(requests apt-requests)" = 0 ] || fail 'apt-get ran without a complete bundle'
consume
rm "$work/consumer/ci/downloads/$corpus_digest"
if offline_env bash "$work/tools/ci/fetch-corpus.sh" "$work/read/missing" 2> "$work/log"; then fail 'a missing corpus was read'; fi
grep -q 'forbids downloading it' "$work/log" || fail 'the missing corpus was not reported'
rm "$work/consumer/ci/downloads/$lua_digest"
if offline_env bash "$offline" lua "$work/read/missing-lua" 2> "$work/log"; then fail 'a missing Lua archive was read'; fi
grep -q 'forbids downloading it' "$work/log" || fail 'the missing Lua archive was not reported'
test ! -e "$work/read/missing-lua" || fail 'a missing Lua archive was extracted'
rm -rf "$work/consumer/ci/sqlite.git"
if offline_env bash "$work/tools/ci/fetch-sqlite.sh" "$work/read/no-sqlite" 2> "$work/log"; then fail 'a missing mirror was fetched around'; fi
grep -q 'forbids fetching it' "$work/log" || fail 'the missing mirror was not reported'
test ! -e "$work/consumer/ci/sqlite.git" || fail 'an offline fetch created a mirror'
[ "$(requests curl-requests)" = 0 ] || fail 'a missing file was downloaded'
printf 'stray\n' > "$work/consumer/apt/stray.deb"
if bash "$offline" verify "$work/consumer" 2> "$work/log"; then fail 'an unlisted .deb was accepted'; fi
grep -q "holds stray.deb, which its MANIFEST doesn't list" "$work/log" || fail 'the unlisted .deb was not named'

# A bad hash: rejected, kept as it is, and never downloaded again by a consumer.
consume
printf 'tampered\n' >> "$work/consumer/apt/gdb_2.0_$native.deb"
if bash "$offline" verify "$work/consumer" 2> "$work/log"; then fail 'a tampered .deb was accepted'; fi
grep -q "gdb_2.0_$native.deb does not match its SHA-256" "$work/log" || fail 'the tampered .deb was not named'
consume
printf 'tampered\n' > "$work/consumer/ci/downloads/$corpus_digest"
if offline_env bash "$work/tools/ci/fetch-corpus.sh" "$work/read/tampered" 2> "$work/log"; then fail 'a tampered corpus was read'; fi
[ "$(cat "$work/consumer/ci/downloads/$corpus_digest")" = tampered ] || fail 'an offline read replaced the tampered copy'
printf 'tampered\n' > "$work/consumer/ci/downloads/$lua_digest"
if offline_env bash "$offline" lua "$work/read/tampered-lua" 2> "$work/log"; then fail 'a tampered Lua archive was read'; fi
[ "$(cat "$work/consumer/ci/downloads/$lua_digest")" = tampered ] || fail 'an offline Lua read replaced the tampered copy'
test ! -e "$work/read/tampered-lua" || fail 'a tampered Lua archive was extracted'
find "$work/consumer/ci/c-testsuite.git/objects" -type f ! -name '*.idx' -exec sh -c 'printf garbage > "$1"' _ {} \;
if offline_env bash "$work/tools/ci/fetch-c-testsuite.sh" "$work/read/tampered-suite" 2> "$work/log"; then cat "$work/log" >&2; fail 'a tampered mirror was read'; fi
grep -q 'forbids fetching it' "$work/log" || fail 'the tampered mirror was not reported'
[ "$(requests curl-requests)" = 0 ] || fail 'a tampered file was downloaded'
for value in 2 yes; do
    if BUSTER_CI_CACHE="$work/consumer/ci" BUSTER_CI_OFFLINE=$value bash "$work/tools/ci/cached-download.sh" https://fixture.invalid/corpus.tar "$corpus_digest" "$work/read/x" 2> /dev/null; then
        fail "BUSTER_CI_OFFLINE=$value was accepted"
    fi
done
if BUSTER_CI_OFFLINE=1 bash "$work/tools/ci/cached-download.sh" https://fixture.invalid/corpus.tar "$corpus_digest" "$work/read/x" 2> /dev/null; then fail 'offline without a bundle downloaded'; fi
if BUSTER_CI_OFFLINE=1 bash "$work/tools/ci/fetch-c-testsuite.sh" "$work/read/no-bundle" 2> /dev/null; then fail 'an offline fetch without a bundle fetched'; fi
[ "$(requests curl-requests)" = 0 ] || fail 'an offline read without a bundle downloaded'

# The preparation repairs what it finds missing or altered, and acquires nothing else.
restore_sources
cp -a "$work/prepared" "$work/damaged"
rm "$work/damaged/apt/tcc_2.0_$native.deb"
printf 'tampered\n' > "$work/damaged/ci/downloads/$corpus_digest"
reset_requests
bash "$offline" prepare "$work/damaged" 2> "$work/log" || { cat "$work/log" >&2; fail 'a damaged bundle was not repaired'; }
grep -q 'input=apt source=network reason=.*tcc_2.0.* is missing' "$work/log" || fail 'the missing .deb was not the reason'
grep -q 'input=corpus source=network' "$work/log" || fail 'the tampered corpus was not acquired again'
for input in baseline c-testsuite sqlite lua; do
    grep -q "input=$input source=cache" "$work/log" || fail "the intact $input was acquired again"
done
[ "$(grep -c . "$work/curl-requests")" = 1 ] && grep -q 'corpus.tar' "$work/curl-requests" || fail 'the repair downloaded more than the corpus'
bash "$offline" verify "$work/damaged" 2> /dev/null || fail 'the repaired bundle does not check out'

# A changed runner image: the preparation resolves the closure again, and the rest stays the cache's.
cp -a "$work/prepared" "$work/rolled"
reset_requests
ImageVersion=20261108.2 bash "$offline" prepare "$work/rolled" 2> "$work/log" || { cat "$work/log" >&2; fail 'a new image'"'"'s preparation failed'; }
grep -q 'input=apt source=network reason=it was resolved on runner image ubuntu26 20261101.1 ' "$work/log" || fail 'the image change was not the reason'
grep -qx "runner ubuntu26 20261108.2 .* $native" "$work/rolled/apt/MANIFEST" || fail 'the new image was not recorded'
for input in baseline corpus c-testsuite sqlite lua; do
    grep -q "input=$input source=cache" "$work/log" || fail "$input was acquired again for a new image"
done
[ "$(requests curl-requests)" = 0 ] || fail 'a new image downloaded the pinned files'

# A consumer drawn on another image, as during a rollout: it installs the bundle, warns, and reports the image's own
# packages that differ; where the closure no longer installs there, it fails, naming both images.
consume
image_status | sed "s/^gcc $native 1.0$/gcc $native 1.1/" > "$work/status"
export GITHUB_STEP_SUMMARY="$work/summary"
: > "$work/summary"
ImageVersion=20261108.2 bash "$offline" apt-install "$work/consumer" > /dev/null 2> "$work/log" || { cat "$work/log" >&2; fail 'another image'"'"'s consumer failed'; }
grep -q "::warning title=Offline hosted inputs::this runner's image (ubuntu26 20261108.2 .*) is not the one the apt bundle was resolved on" "$work/log" || fail 'the image change was not warned about'
grep -q "gcc:$native is the runner image's 1.1, not the recorded 1.0" "$work/log" || fail 'the image'"'"'s different gcc was not reported'
grep -qx "| \`gcc:$native\` | \`1.1\` | the runner image (recorded 1.0) |" "$work/summary" || fail 'the summary does not show the difference'
consume
image_status | grep -v '^libimage ' > "$work/status"
if ImageVersion=20261108.2 bash "$offline" apt-install "$work/consumer" > /dev/null 2> "$work/log"; then fail 'a closure that does not install on another image succeeded'; fi
grep -q 'resolved on runner image ubuntu26 20261101.1 .*, do not install on this one (ubuntu26 20261108.2 ' "$work/log" || fail 'the failure does not name both images'
consume
image_status | sed "s/^gcc $native 1.0$/gcc $native 1.1/" > "$work/status"
if bash "$offline" apt-install "$work/consumer" > /dev/null 2> "$work/log"; then fail 'the bundle'"'"'s own image with another gcc was accepted'; fi
grep -q "gcc:$native is installed at 1.1, not at the recorded 1.0" "$work/log" || fail 'the changed gcc was not named'
unset GITHUB_STEP_SUMMARY

# A changed package list, release or architecture, or a malformed manifest: the bundle is not this setup's.
check_manifest()
{
    local expression=$1 message=$2
    consume
    sed -i "$expression" "$work/consumer/apt/MANIFEST"
    if bash "$offline" verify "$work/consumer" 2> "$work/log"; then fail "accepted: $message"; fi
}
check_manifest 's/^request gcc /request clang /' 'another package list'
grep -q "its package list is not setup-hosted.sh's" "$work/log" || fail 'the package list was not the reason'
check_manifest "s/^\\(runner [^ ]* [^ ]* \\)[^ ]*/\\1 20.04/" 'another release'
grep -q "not for this runner's release and architecture" "$work/log" || fail 'the release was not the reason'
check_manifest 's/^format .*/format buster-hosted-apt 0/' 'another format'
check_manifest "/^image gcc:/d" 'a package without a version'
grep -q 'records no version of gcc' "$work/log" || fail 'the unrecorded gcc was not the reason'
check_manifest "s/^bundle tcc:$native 2.0 [0-9a-f]*/bundle tcc:$native 2.0 xyz/" 'a malformed digest'
check_manifest '$a bundle tcc:amd64 2.0 0000000000000000000000000000000000000000000000000000000000000000 ../escape.deb' 'a path outside the bundle'
check_manifest 's/^image g++:/image gcc:/' 'a package recorded twice'
grep -q 'records gcc:.* twice' "$work/log" || fail 'the duplicate was not the reason'
check_manifest '$a mirror x' 'an unknown record'
if bash "$offline" verify "$work/absent" 2> /dev/null; then fail 'a missing bundle was accepted'; fi
if bash "$offline" prepare / 2> /dev/null; then fail 'the root directory was prepared'; fi
# A cache miss while the runner can't resolve Ubuntu's archive: the preparation pins it through DNS over HTTPS before
# its network attempts, as setup-hosted.sh did, and leaves the names that resolve alone.
printf 'archive.ubuntu.com\n' > "$work/unresolvable"
reset_requests
ImageVersion=20261115.1 bash "$offline" prepare "$work/rolled" 2> "$work/log" || { cat "$work/log" >&2; fail 'a preparation with an unresolvable archive failed'; }
grep -q 'archive.ubuntu.com does not resolve here; pinned it to 192.0.2.7 through DNS over HTTPS' "$work/log" || fail 'the archive was not pinned'
grep -qx '192.0.2.7 archive.ubuntu.com' "$work/hosts" || fail 'the pin is not in the hosts file'
[ "$(grep -c . "$work/hosts")" = 1 ] || fail 'a name that resolves was pinned'
grep -q 'cloudflare-dns.com/dns-query?name=archive.ubuntu.com&type=A' "$work/curl-requests" || fail 'the pin did not ask Cloudflare'
# The drill's outage (tools/ci/hosts-down.sh) replaces a host's earlier entries, such as those pins, rather than adding
# to them: glibc returns every matching line, so a pin left beside the loopback entry kept the host reachable.
printf '127.0.0.1 localhost\n# pinned\n192.0.2.7 archive.ubuntu.com mirror.example # pin\n198.51.100.1 GitHub.com\n' > "$work/drill-hosts"
BUSTER_HOSTS_FILE="$work/drill-hosts" bash "$root/tools/ci/hosts-down.sh" archive.ubuntu.com github.com || fail 'the drill could not edit its table'
[ "$(cat "$work/drill-hosts")" = "$(printf '127.0.0.1 localhost\n# pinned\n192.0.2.7 mirror.example # pin\n127.0.0.1 archive.ubuntu.com\n::1 archive.ubuntu.com\n127.0.0.1 github.com\n::1 github.com')" ] || { cat "$work/drill-hosts" >&2; fail 'the drill left an earlier address for a host it took down'; }
printf 'hosted-offline: cold (retried) and warm preparation, the consumer'"'"'s offline install and reads, missing files, bad hashes, image and package-list changes, repairs and DNS-over-HTTPS pins checked\n'
