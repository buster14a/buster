#!/usr/bin/env bash
set -euo pipefail

# Main is expected to accept only a PR head SHA that already ran the full
# matrix. This gate verifies that through Forgejo's commit-status API instead
# of assuming the merge strategy. If the pushed SHA does not carry a passing
# verdict for every required context, the matrix must run. Any API or parsing
# failure resolves to "untested" so CI fails closed.
#
# Forgejo publishes a `pending` status for every job in this run, including the
# matrix jobs this gate decides about, before the gate job starts. The newest
# status for a required context therefore always belongs to the run being
# gated and never carries a verdict. Only a terminal status (`success`,
# `failure`, `error`, `skipped`, `cancelled`) says anything about whether the
# SHA was tested, so each required context resolves to its newest terminal
# status and all four must be `success`.
tested=false

if [[ -n "${REPO:-}" && -n "${SHA:-}" && -n "${TOKEN:-}" ]]; then
    tested="$(
        SERVER_URL="${GITHUB_SERVER_URL:-https://code.buster14a.com}" \
            python3 - <<'PY' || true
import datetime
import json
import os
import sys
import urllib.request

# Must match the runner matrix in .forgejo/workflows/ci.yml; a context missing
# from this list is not proven by the gate.
REQUIRED = (
    "/ x86_64-linux (push)",
    "/ aarch64-macos-mini (push)",
    "/ x86_64-linux-dedicated (push)",
    "/ x86_64-windows-znver5 (push)",
)

PAGE_SIZE = 100
MAX_PAGES = 20


def fetch(page):
    url = "%s/api/v1/repos/%s/statuses/%s?limit=%d&page=%d" % (
        os.environ["SERVER_URL"].rstrip("/"),
        os.environ["REPO"],
        os.environ["SHA"],
        PAGE_SIZE,
        page,
    )
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": "token " + os.environ["TOKEN"],
            # The default Python-urllib agent is rejected with error 1010 by the
            # edge in front of the Forgejo instance.
            "User-Agent": "buster-ci-main-gate",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


statuses = []
for page in range(1, MAX_PAGES + 1):
    batch = fetch(page)
    if not batch:
        break
    statuses.extend(batch)

EPOCH = datetime.datetime.min.replace(tzinfo=datetime.timezone.utc)


def created_at(status):
    # Timestamps carry a UTC offset that changes across DST, so compare parsed
    # instants rather than strings. An unparseable timestamp sorts oldest and
    # therefore cannot outrank a real verdict.
    try:
        return datetime.datetime.fromisoformat(status.get("created_at") or "")
    except ValueError:
        return EPOCH


# The API returns newest first, but statuses created within the same second come
# back in arbitrary order, so order explicitly and break ties by status id.
statuses.sort(key=lambda status: (created_at(status), status.get("id") or 0), reverse=True)

verdicts = {}
for status in statuses:
    context = status.get("context")
    state = status.get("status")
    if context in REQUIRED and context not in verdicts and state != "pending":
        verdicts[context] = state

for context in REQUIRED:
    print(
        "CI_MAIN_GATE context=%s verdict=%s" % (context, verdicts.get(context, "none")),
        file=sys.stderr,
    )

print("true" if all(verdicts.get(c) == "success" for c in REQUIRED) else "false")
PY
    )"
fi

# Anything other than a clean "true" — an empty capture from a failed lookup
# included — leaves the matrix running.
if [[ "${tested}" != "true" ]]; then
    tested=false
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "tested=${tested}" >> "${GITHUB_OUTPUT}"
fi
printf 'CI_MAIN_GATE sha=%s tested=%s\n' "${SHA:-}" "${tested}"
