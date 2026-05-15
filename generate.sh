#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

PYTHON=()
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; raise SystemExit(sys.version_info[0] != 3)' >/dev/null 2>&1; then
        PYTHON=("$candidate")
        break
    fi
done

if [[ ${#PYTHON[@]} -eq 0 ]] && command -v py >/dev/null 2>&1 && py -3 -c 'import sys' >/dev/null 2>&1; then
    PYTHON=(py -3)
fi

if [[ ${#PYTHON[@]} -eq 0 ]]; then
    echo "error: Python 3 is required" >&2
    STATUS=127
    if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
        return "$STATUS"
    fi
    exit "$STATUS"
fi

if "${PYTHON[@]}" "$SCRIPT_DIR/generate.py" "$@"; then
    STATUS=0
else
    STATUS=$?
fi

if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
    return "$STATUS"
fi
exit "$STATUS"
