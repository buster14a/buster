#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# #146's Tcl for SQLite's testfixture (test-sqlite-testfixture): prints the directory that holds Tcl 8.6's tclConfig.sh,
# from the distribution's development package (Ubuntu's and Debian's tcl8.6-dev, Arch's tcl). On a GitHub-hosted runner
# the package must be the pinned one, which tools/ci/setup-hosted.sh installs: a different version fails, to be pinned
# again here. Elsewhere, a host without the package exits 77, the inventory's skip, after saying so; the version found
# is printed on standard error, for the log's provenance.
set -euo pipefail
pinned=8.6.17+dfsg-1build1
directory=
for candidate in /usr/lib/tcl8.6 /usr/lib/x86_64-linux-gnu/tcl8.6 /usr/lib64 /usr/lib; do
    # Debian's /usr/lib/tcl8.6/tclConfig.sh sources the multiarch one; a subshell reads each.
    if [ -f "$candidate/tclConfig.sh" ] && [ "$(. "$candidate/tclConfig.sh" && printf '%s' "$TCL_VERSION")" = 8.6 ]; then
        directory=$candidate
        break
    fi
done
hosted=false
if [ "${RUNNER_ENVIRONMENT:-}" = github-hosted ]; then hosted=true; fi
if [ -z "$directory" ]; then
    if [ "$hosted" = true ]; then
        printf 'tcl-config.sh: no Tcl 8.6 tclConfig.sh on this hosted runner (tools/ci/setup-hosted.sh installs tcl8.6-dev)\n' >&2
        exit 1
    fi
    printf 'SKIP: SQLite'"'"'s testfixture needs Tcl 8.6'"'"'s development package (tcl8.6-dev, or Arch'"'"'s tcl), which this host lacks\n' >&2
    exit 77
fi
if command -v dpkg-query > /dev/null 2>&1 && version=$(dpkg-query -W -f='${Version}' tcl8.6-dev 2> /dev/null); then
    printf 'tcl-config.sh: tcl8.6-dev %s, %s\n' "$version" "$directory" >&2
    if [ "$hosted" = true ] && [ "$version" != "$pinned" ]; then
        printf 'tcl-config.sh: tcl8.6-dev is %s, not the pinned %s: pin it again in tools/ci/tcl-config.sh\n' "$version" "$pinned" >&2
        exit 1
    fi
elif command -v pacman > /dev/null 2>&1 && version=$(LC_ALL=C pacman -Q tcl 2> /dev/null); then
    printf 'tcl-config.sh: %s, %s\n' "$version" "$directory" >&2
fi
printf '%s\n' "$directory"
