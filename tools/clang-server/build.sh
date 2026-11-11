#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Builds the clang server (clang_server.cpp, #2071) against the libraries of the clang on PATH, whose release llvm-config
# must be: the server's runs stand for that clang's. bash tools/clang-server/build.sh [OUTPUT], by default
# build/clang-server/clang-server; CXX and LLVM_CONFIG choose the tools.
#
# The server must hold one copy of LLVM. Where LLVM is a shared library (llvm-config --shared-mode: a distribution's),
# libclang-cpp links to it, and so does the server. Where it is static (the official release), libclang-cpp bundles
# LLVM without exporting the targets' initializers, and LLVM's static archives beside it would bring a second copy of
# its core: two copies' globals, constructed and destroyed twice ("double free" at exit, run 42911952930). There the
# server links clang's static archives with LLVM's instead, and no libclang-cpp: one copy, its own. Where the toolchain
# has no clang archives (an installation that left them out), it links libclang-cpp alone if that suffices, and
# otherwise exits with status 77, which a caller reads as no server for this toolchain. Any other status is a failure.
#
# The built server must report the clang's release (--version) and answer a request as a spawned clang does: one
# -fsyntax-only run of a one-line file with a warning, through the protocol of clang_server.cpp's header comment, in the
# same directory and environment, with the same status and standard error.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
output=${1:-"$root/build/clang-server/clang-server"}
compiler=${CXX:-clang++}
config=${LLVM_CONFIG:-llvm-config}
version=$(clang -dumpversion)
if [ "$("$config" --version)" != "$version" ]; then
    echo "clang-server: $config is LLVM $("$config" --version), clang is $version" >&2
    exit 1
fi
words()
{
    local line
    local part
    while IFS= read -r line; do
        read -r -a part <<< "$line"
        printf '%s\n' "${part[@]}"
    done <<< "$1"
}
mapfile -t flags < <(words "$("$config" --cxxflags --ldflags)")
library_directory=$("$config" --libdir)
libraries=()
if [ "$("$config" --shared-mode)" = shared ]; then
    mapfile -t system < <(words "$("$config" --system-libs)")
    mapfile -t llvm < <(words "$("$config" --libs)")
    libraries=(-lclang-cpp "${llvm[@]}")
    link=shared
elif [ -f "$library_directory/libclangDriver.a" ] && [ -f "$library_directory/libclangFrontendTool.a" ]; then
    # Clang's archives refer to each other in cycles, so the linker reads them as a group; LLVM's follow in
    # llvm-config's dependency order.
    mapfile -t system < <(words "$("$config" --link-static --system-libs)")
    mapfile -t llvm < <(words "$("$config" --link-static --libs)")
    libraries=(-Wl,--start-group "$library_directory"/libclang*.a -Wl,--end-group "${llvm[@]}")
    link=static
else
    mapfile -t system < <(words "$("$config" --system-libs)")
    libraries=(-lclang-cpp)
    link=bundled
fi
mkdir -p "$(dirname "$output")"
log=$(mktemp)
smoke=$(mktemp -d)
trap 'rm -rf "$log" "$smoke"' EXIT
status=0
"$compiler" -O2 -Wall -Werror "$root/tools/clang-server/clang_server.cpp" "${flags[@]}" "${libraries[@]}" "${system[@]}" -o "$output" 2> "$log" || status=$?
if [ "$status" -ne 0 ] && [ "$link" = bundled ] && grep -qE 'undefined (reference|symbol)' "$log"; then
    echo "clang-server: this toolchain's libclang-cpp bundles LLVM without what the server needs (static LLVM), and it has no clang archives: no server" >&2
    grep -m 3 -E 'undefined (reference|symbol)' "$log" >&2
    exit 77
fi
cat "$log" >&2
[ "$status" -eq 0 ]
test "$("$output" --version)" = "$version"
# The smoke request. LC_ALL=C makes a string's ${#} its length in bytes, and is in both runs' environment.
export LC_ALL=C
integer()
{
    printf '%b' "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' "$(($1 & 255))" "$(($1 >> 8 & 255))" "$(($1 >> 16 & 255))" "$(($1 >> 24 & 255))")"
}
string()
{
    integer "${#1}"
    printf '%s' "$1"
}
printf 'int main(void) { return 1 << 40; }\n' > "$smoke/smoke.c"
arguments=(clang -fsyntax-only smoke.c)
mapfile -d '' -t environment < <(env -0)
{
    integer "${#arguments[@]}"
    integer 2
    integer "${#environment[@]}"
    string "$smoke"
    for argument in "${arguments[@]}"; do
        string "$argument"
    done
    for variable in "${environment[@]}"; do
        string "$variable"
    done
} > "$smoke/request"
"$output" < "$smoke/request" > "$smoke/reply"
served_status=$(od -An -t d4 -N 4 "$smoke/reply" | tr -d ' ')
served_length=$(od -An -t u4 -j 4 -N 4 "$smoke/reply" | tr -d ' ')
tail -c +9 "$smoke/reply" > "$smoke/served"
spawned_status=0
(cd "$smoke" && "${arguments[@]}" > /dev/null 2> "$smoke/spawned") || spawned_status=$?
if [ "$(stat -c %s "$smoke/served")" != "$served_length" ] || [ "$served_status" != "$spawned_status" ] ||
    ! cmp -s "$smoke/served" "$smoke/spawned" || ! grep -q 'warning:' "$smoke/spawned"; then
    printf 'clang-server: the smoke request differs from a spawned clang: served status %s, %s bytes:\n' "$served_status" "$served_length" >&2
    cat "$smoke/served" >&2
    printf 'spawned status %s:\n' "$spawned_status" >&2
    cat "$smoke/spawned" >&2
    exit 1
fi
printf 'clang-server: %s (clang %s, %s link), smoke request as a spawned clang: status %s, %s bytes of warnings\n' "$output" "$version" "$link" "$served_status" "$served_length"
