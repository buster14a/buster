#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${BUSTER_BUILD_DIRECTORY:-}" ]]; then
    BUSTER_BUILD_DIRECTORY=build
fi

ninja -C "$BUSTER_BUILD_DIRECTORY" "$@"
exit $?
