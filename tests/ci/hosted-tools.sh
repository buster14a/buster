#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Real hashing/publication with tiny fixture inputs; no remote requests or retry sleeps.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'hosted-tools fixture: %s\n' "$*" >&2; exit 1; }
mkdir -p "$work/tools" "$work/bin" "$work/upstream"
cp "$root/tools/ci/hosted-release.sh" "$work/tools/"
head -n 1 "$root/tools/ci/hosted-tools.lock" > "$work/tools/hosted-tools.lock"
for name in llvm llvm-license cmake ninja mold wild; do
    printf 'fixture:%s\n' "$name" > "$work/upstream/$name"
    digest=$(sha256sum "$work/upstream/$name" | cut -c 1-64)
    printf '%s 1.2.3 %s https://fixture.invalid/%s\n' "$name" "$digest" "$name" >> "$work/tools/hosted-tools.lock"
done
cat > "$work/bin/curl" <<'CURL'
#!/usr/bin/env bash
set -euo pipefail
printf '%q ' "$@" >> "$FIXTURE/curl-arguments"
printf '\n' >> "$FIXTURE/curl-arguments"
output=''
while [ $# -gt 0 ]; do
    case "$1" in --output) output=$2; shift ;; https://*) url=$1 ;; esac
    shift
done
printf '%s\n' "$url" >> "$FIXTURE/requests"
[ "${FAIL_DOWNLOAD:-}" != yes ] || exit 35
cp "$FIXTURE/upstream/${url##*/}" "$output"
CURL
chmod +x "$work/bin/curl"
export FIXTURE="$work" PATH="$work/bin:$PATH"
release="$work/tools/hosted-release.sh"
bash "$release" prepare "$work/cache"
[ "$(wc -l < "$work/requests")" = 6 ] || fail 'cold acquisition is not once per component'
bash "$release" verify "$work/cache"
cp "$work/requests" "$work/requests.before"
mv "$work/upstream" "$work/upstream.hidden"
bash "$release" prepare "$work/cache"
cmp "$work/requests" "$work/requests.before" || fail 'warm preparation contacted upstream'
BUSTER_HOSTED_RELEASES="$work/cache" bash "$release" get llvm "$work/consumer"
cmp "$work/consumer" "$work/cache/llvm"
# Corruption cannot become a per-shard download. Empty/missing handoffs also fail.
printf 'corrupt\n' > "$work/cache/llvm"
if BUSTER_HOSTED_RELEASES="$work/cache" bash "$release" get llvm "$work/consumer"; then fail 'consumer accepted corruption'; fi
if BUSTER_HOSTED_RELEASES='' bash "$release" get llvm "$work/consumer"; then fail 'empty handoff fell back'; fi
if BUSTER_HOSTED_RELEASES="$work/absent" bash "$release" get llvm "$work/consumer"; then fail 'missing handoff fell back'; fi
cmp "$work/requests" "$work/requests.before" || fail 'a consumer contacted upstream'
# Only the coordinator reacquires the damaged component.
mv "$work/upstream.hidden" "$work/upstream"
bash "$release" prepare "$work/cache"
[ "$(wc -l < "$work/requests")" = 7 ] || fail 'corruption recovery downloaded unrelated inputs'
# A changed response fails its original hash and publishes no partial output.
printf 'changed\n' > "$work/upstream/llvm"
if bash "$release" prepare "$work/bad"; then fail 'wrong digest was published'; fi
[ ! -e "$work/bad" ] || fail 'partial preparation survived'
if compgen -G "$work/bad.prepare.*" > /dev/null; then fail 'staging survived failure'; fi
# A failed transfer preserves a previously installed output.
printf 'sentinel\n' > "$work/output"
if FAIL_DOWNLOAD=yes bash "$release" get llvm "$work/output"; then fail 'transport failure succeeded'; fi
[ "$(cat "$work/output")" = sentinel ] || fail 'failed transfer replaced output'
grep -q -- '--connect-timeout 15' "$work/curl-arguments" || fail 'missing connection deadline'
grep -q -- '--max-time 90' "$work/curl-arguments" || fail 'missing transfer deadline'
# Symlinks in restored input positions are not accepted as verified regular files.
rm "$work/cache/llvm"
ln -s "$work/consumer" "$work/cache/llvm"
if bash "$release" verify "$work/cache"; then fail 'input symlink was accepted'; fi
# The cache is not the schema authority; malformed/duplicate/platform pins fail.
cp "$work/tools/hosted-tools.lock" "$work/good.lock"
sed -i '1s/linux-x86_64/linux-aarch64/' "$work/tools/hosted-tools.lock"
if bash "$release" version llvm; then fail 'wrong platform was accepted'; fi
cp "$work/good.lock" "$work/tools/hosted-tools.lock"
tail -n 1 "$work/good.lock" >> "$work/tools/hosted-tools.lock"
if bash "$release" version llvm; then fail 'duplicate component was accepted'; fi
cp "$work/good.lock" "$work/tools/hosted-tools.lock"
if bash "$release" get '../escape' "$work/output"; then fail 'unsafe component name was accepted'; fi
# A saved artifact is simply the same original bytes: no cache service is needed.
rm "$work/cache/llvm"
cp "$work/consumer" "$work/cache/llvm"
cp -a "$work/cache" "$work/handoff"
rm -rf "$work/cache"
bash "$release" verify "$work/handoff"
BUSTER_HOSTED_RELEASES="$work/handoff" bash "$release" get llvm "$work/output"
cmp "$work/output" "$work/consumer"
printf 'hosted-tools: cold/warm, corruption, offline consumers, handoff, schema and atomic failure fixtures passed\n'
# Exercise the real extraction recipe and relocation with tiny synthetic releases.
# These stubs check installer mechanics, not the behavior of the official tools.
cp "$root/tools/ci/setup-build-tools.sh" "$root/tools/ci/install-hosted-tools.sh" "$work/tools/"
mkdir -p "$work/archive/LLVM-23.1.4-Linux-X64/bin" "$work/archive/LLVM-23.1.4-Linux-X64/include" "$work/archive/LLVM-23.1.4-Linux-X64/lib/clang/23/lib/test"
llvm="$work/archive/LLVM-23.1.4-Linux-X64"
cat > "$llvm/bin/clang-23" <<'CLANG'
#!/usr/bin/env bash
case "$1" in -dumpversion) echo 23.1.4 ;; -print-runtime-dir) printf '%s/../lib/clang/23/lib/test\n' "$(dirname "$0")" ;; *) exit 1 ;; esac
CLANG
chmod +x "$llvm/bin/clang-23"
ln -s clang-23 "$llvm/bin/clang"
ln -s clang-23 "$llvm/bin/clang++"
for tool in lld llvm-dwarfdump llvm-readobj llvm-symbolizer; do printf '#!/bin/sh\nexit 0\n' > "$llvm/bin/$tool"; chmod +x "$llvm/bin/$tool"; done
ln -s lld "$llvm/bin/ld.lld"
printf '#!/bin/sh\necho 23.1.4\n' > "$llvm/bin/llvm-config"
chmod +x "$llvm/bin/llvm-config"
touch "$llvm/include/header.h" "$llvm/lib/libLLVMfixture.a" "$llvm/lib/libPollyfixture.a" "$llvm/lib/libclang-cpp.so.23" "$llvm/lib/clang/23/lib/test/libclang_rt.fuzzer_no_main.a"
tar --zstd -cf "$work/upstream/llvm" -C "$work/archive" LLVM-23.1.4-Linux-X64
cmake="$work/archive/cmake-4.4.3-linux-x86_64"
mkdir -p "$cmake/bin" "$cmake/share/cmake-4.4" "$cmake/doc/cmake"
cat > "$cmake/bin/cmake" <<'CMAKE'
#!/usr/bin/env bash
if [ "$1" = --version ]; then echo 'cmake version 4.4.3'; else tar -xf "$4"; fi
CMAKE
printf '#!/bin/sh\necho "ctest version 4.4.3"\n' > "$cmake/bin/ctest"
chmod +x "$cmake/bin/"*
printf 'fixture license\n' > "$cmake/doc/cmake/LICENSE.rst"
tar -czf "$work/upstream/cmake" -C "$work/archive" cmake-4.4.3-linux-x86_64
mkdir "$work/ninja"
printf '#!/bin/sh\necho 1.13.2\n' > "$work/ninja/ninja"
tar -cf "$work/upstream/ninja" -C "$work/ninja" ninja
mold="$work/archive/mold-3.0.0-x86_64-linux"
mkdir -p "$mold/bin" "$mold/share/doc/mold"
printf '#!/bin/sh\necho "mold 3.0.0 (8de38c35a2df16a25f7ff87ac3ad07156a925beb; compatible with GNU ld)"\n' > "$mold/bin/mold"
chmod +x "$mold/bin/mold"
printf 'fixture license\n' > "$mold/share/doc/mold/LICENSE"
tar -czf "$work/upstream/mold" -C "$work/archive" mold-3.0.0-x86_64-linux
mkdir "$work/wild"
printf '#!/bin/sh\necho "Wild version 0.10.0"\n' > "$work/wild/wild"
chmod +x "$work/wild/wild"
tar -czf "$work/upstream/wild" -C "$work/wild" wild
head -n 1 "$root/tools/ci/hosted-tools.lock" > "$work/tools/hosted-tools.lock"
while read -r name version digest url extra; do
    [[ -z "$name" || "$name" = \#* ]] && continue
    digest=$(sha256sum "$work/upstream/$name" | cut -c 1-64)
    printf '%s %s %s https://fixture.invalid/%s\n' "$name" "$version" "$digest" "$name" >> "$work/tools/hosted-tools.lock"
done < "$root/tools/ci/hosted-tools.lock"
bash "$release" prepare "$work/real-inputs"
BUSTER_HOSTED_RELEASES="$work/real-inputs" bash "$work/tools/install-hosted-tools.sh" "$work/installed tree"
[ "$("$work/installed tree/llvm-23.1.4/bin/clang" -dumpversion)" = 23.1.4 ] || fail 'relocated compiler failed'
[ -x "$work/installed tree/bin/wild" ] && [ -x "$work/installed tree/bin/mold" ] || fail 'relocated links broke'
[ -f "$("$work/installed tree/llvm-23.1.4/bin/clang" -print-runtime-dir)/libclang_rt.fuzzer_no_main.a" ] || fail 'relocated runtime missing'
# An internally incomplete but hash-pinned fixture must fail without publishing.
rm "$llvm/lib/clang/23/lib/test/libclang_rt.fuzzer_no_main.a"
tar --zstd -cf "$work/upstream/llvm" -C "$work/archive" LLVM-23.1.4-Linux-X64
digest=$(sha256sum "$work/upstream/llvm" | cut -c 1-64)
sed -i "s/^llvm 23.1.4 [0-9a-f]* /llvm 23.1.4 $digest /" "$work/tools/hosted-tools.lock"
bash "$release" prepare "$work/real-inputs"
if BUSTER_HOSTED_RELEASES="$work/real-inputs" bash "$work/tools/install-hosted-tools.sh" "$work/incomplete"; then fail 'missing runtime was accepted'; fi
[ ! -e "$work/incomplete" ] || fail 'incomplete install was published'
if compgen -G "$work/incomplete.install.*" > /dev/null; then fail 'incomplete extraction staging survived'; fi
printf 'hosted-tools: extraction, relocation, runtime probes and incomplete-publication fixtures passed\n'
