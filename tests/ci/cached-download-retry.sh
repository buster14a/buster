#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# No retry sleeps or network: inspect the production command through a stand-in transport.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail() { printf 'cached-download retry fixtures: %s\n' "$*" >&2; exit 1; }
mkdir "$work/bin"
export FAKE_CURL_SOURCE="$work/source" FAKE_CURL_LOG="$work/arguments" FAKE_CURL_MODE=success
printf 'verified source\n' > "$FAKE_CURL_SOURCE"
digest=$(sha256sum "$FAKE_CURL_SOURCE" | cut -c 1-64)
cat > "$work/bin/curl" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$@" > "$FAKE_CURL_LOG"
output=''
while [ "$#" -gt 0 ]; do
    case "$1" in --output) output=$2; shift 2 ;; *) shift ;; esac
done
[ -n "$output" ]
case "$FAKE_CURL_MODE" in
    transport) printf 'partial transport\n' > "$output"; exit 7 ;;
    mismatch) printf 'wrong content\n' > "$output" ;;
    *) cp "$FAKE_CURL_SOURCE" "$output" ;;
esac
SH
chmod +x "$work/bin/curl"
export PATH="$work/bin:$PATH" BUSTER_CI_CACHE=''
helper="$root/tools/ci/cached-download.sh"
for protocol in file FILE http https; do
    bash "$helper" "$protocol://fixture.invalid/source" "$digest" "$work/output"
    cmp "$FAKE_CURL_SOURCE" "$work/output"
    for option in --fail --location --silent --show-error --output; do
        grep -Fxq -- "$option" "$FAKE_CURL_LOG" || fail "missing $option ($protocol)"
    done
    case "$protocol" in
        file|FILE)
            if grep -q '^--retry' "$FAKE_CURL_LOG"; then fail 'local-file failures still back off'; fi ;;
        *)
            grep -Fxq -- --retry-all-errors "$FAKE_CURL_LOG" || fail 'network resets lost their retry policy'
            [ "$(awk '/^--retry$/ { getline; print }' "$FAKE_CURL_LOG")" = 8 ] || fail 'network retry count changed'
            [ "$(awk '/^--retry-max-time$/ { getline; print }' "$FAKE_CURL_LOG")" = 240 ] || fail 'network retry budget changed' ;;
    esac
done
# A failed transfer may leave partial bytes, but never a cache entry or a published output.
export BUSTER_CI_CACHE="$work/cache"
status=0
FAKE_CURL_MODE=transport bash "$helper" https://fixture.invalid/source "$digest" "$work/failed-output" || status=$?
[ "$status" -eq 7 ] || fail 'the transfer failure was lost'
[ ! -e "$BUSTER_CI_CACHE/downloads/$digest" ] && [ ! -e "$work/failed-output" ] || fail 'partial data was published'
[ -z "$(find "$BUSTER_CI_CACHE" -name '.download.*' -print)" ] || fail 'failed transfer temporary was not cleaned'
status=0
FAKE_CURL_MODE=mismatch bash "$helper" https://fixture.invalid/source "$digest" "$work/mismatched-output" 2>/dev/null || status=$?
[ "$status" -ne 0 ] || fail 'a mismatched digest succeeded'
[ ! -e "$BUSTER_CI_CACHE/downloads/$digest" ] && [ ! -e "$work/mismatched-output" ] || fail 'unverified data was published'
[ -z "$(find "$BUSTER_CI_CACHE" -name '.download.*' -print)" ] || fail 'digest-failure temporary was not cleaned'
bash "$helper" https://fixture.invalid/source "$digest" "$work/recovered-output"
cmp "$FAKE_CURL_SOURCE" "$work/recovered-output"
rm "$FAKE_CURL_LOG"
FAKE_CURL_MODE=transport bash "$helper" https://fixture.invalid/source "$digest" "$work/reused-output"
[ ! -e "$FAKE_CURL_LOG" ] || fail 'a verified cache hit contacted the transport'
cmp "$FAKE_CURL_SOURCE" "$work/reused-output"
printf 'cached-download retry fixtures: local/remote arguments, transfer and digest failure, cleanup, recovery and verified reuse passed\n'
