#!/usr/bin/env bash
set -euo pipefail

# Main is expected to accept only a PR head SHA that already ran the full
# matrix. This gate verifies that through Forgejo's commit-status API instead
# of assuming the merge strategy. If the pushed SHA does not carry current
# success statuses for every required context, the matrix must run. Any API
# or parsing failure resolves to "untested" so CI fails closed.
tested=false

if [[ -n "${REPO:-}" && -n "${SHA:-}" && -n "${TOKEN:-}" ]]; then
    server_url="${GITHUB_SERVER_URL:-https://code.buster14a.com}"
    api_url="${server_url}/api/v1/repos/${REPO}/statuses/${SHA}"
    statuses_json="$(curl -fsS --max-time 30 -H "Authorization: token ${TOKEN}" "${api_url}" 2>/dev/null || true)"
    if [[ -n "${statuses_json}" ]]; then
        tested="$(python3 -c 'import json, sys

required = {
    "/ x86_64-linux (push)",
    "/ aarch64-macos-mini (push)",
    "/ x86_64-linux-dedicated (push)",
    "/ x86_64-windows-znver5 (push)",
}

try:
    statuses = json.loads(sys.argv[1])
except Exception:
    sys.exit(1)

latest = {}
for status in statuses:
    context = status.get("context")
    if context in required and context not in latest:
        latest[context] = status.get("status")

print("true" if len(latest) == len(required) and all(value == "success" for value in latest.values()) else "false")
' "${statuses_json}" || true)"
    fi
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "tested=${tested}" >> "${GITHUB_OUTPUT}"
fi
printf 'CI_MAIN_GATE sha=%s tested=%s\n' "${SHA:-}" "${tested}"
