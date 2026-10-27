#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Root-owned wrapper for the existing shared job-started hook. Install only from reviewed main, as documented.
# The original script still owns every detail of the shared lock/holder/lifetime. Other repositories keep their
# existing workflow policy. The flag and guard must live in this root-owned directory, outside runner write access.
set -euo pipefail
hook_root=/usr/local/libexec/buster-9700x
if [ -e "$hook_root/admission-enabled" ]; then
    "$hook_root/admission" --host-source-guard
    if [ "${GITHUB_REPOSITORY:-}" = buster14a/buster ] && { [ ! -w /var/lib/buster-9700x ] || [ ! -w /var/lib/buster-9700x/lock ]; }; then
        echo '9700X admission: Buster cannot use the mandatory shared host lock; refusing.' >&2
        exit 1
    fi
fi
exec "$hook_root/job-started.before-admission.sh" "$@"
