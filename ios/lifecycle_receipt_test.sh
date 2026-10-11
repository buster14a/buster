#!/usr/bin/env bash
# Receipt controls run on system Bash, including macOS Bash 3.2.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$repo_root/ios/lifecycle_receipt.sh"
root=$(mktemp -d "${TMPDIR:-/tmp}/buster-receipt.XXXXXX")
trap 'rm -rf "$root"' EXIT
# The field path must be entirely builtin even if grep/wc are unavailable.
grep() { echo 'receipt launched grep' >&2; return 97; }
wc() { echo 'receipt launched wc' >&2; return 97; }
lifecycle_receipt_field 'BUSTER_IOS_SUPERVISOR version=1 status=0 native_status=143 extra=value' status 0
[[ $lifecycle_receipt_value == 0 ]]
lifecycle_receipt_field 'BUSTER_IOS_SUPERVISOR version=1 signal=15' signal '0|2|15'
[[ $lifecycle_receipt_value == 15 ]]
for row in 'version=1 status=0 status=0' 'status=0 status=1' 'native_status=0' 'status=00' 'status=' 'status=0suffix' 'status=0x'; do
    if lifecycle_receipt_field "$row" status 0; then
        printf 'incorrectly admitted field: %s\n' "$row" >&2
        exit 1
    fi
done
for row in 'signal=150' 'signal=1' 'signal=2 signal=15'; do
    if lifecycle_receipt_field "$row" signal '0|2|15'; then
        printf 'incorrectly admitted signal: %s\n' "$row" >&2
        exit 1
    fi
done
if lifecycle_receipt_read "$root/missing"; then exit 1; fi
: >"$root/receipt"
if lifecycle_receipt_read "$root/receipt"; then exit 1; fi
printf 'version=1 status=0\n' >"$root/receipt"
lifecycle_receipt_read "$root/receipt"
[[ $lifecycle_receipt_line == 'version=1 status=0' ]]
for content in 'unterminated' $'one\ntwo\n' $'one\ntwo' $'one\n\n'; do
    printf '%s' "$content" >"$root/receipt"
    if lifecycle_receipt_read "$root/receipt"; then
        printf 'incorrectly admitted malformed receipt\n' >&2
        exit 1
    fi
done
# Bash's ordinary read discards NULs; the reader must refuse rather than
# reconstruct a valid key/value or ignore a trailing binary suffix.
for content in 'ver\0sion=1 status=0\n' 'version=1 status=\0000\n' 'version=1 status=0\n\0suffix'; do
    printf '%b' "$content" >"$root/receipt"
    if lifecycle_receipt_read "$root/receipt"; then
        printf 'incorrectly admitted binary receipt\n' >&2
        exit 1
    fi
done
printf 'lifecycle receipt missing/duplicate/value/row/binary controls passed\n'
