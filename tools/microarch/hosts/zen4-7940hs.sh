# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The owner's Ryzen 9 7940HS (Zen 4) desktop. Every run takes a grant from the
# `cores` queue (AGENTS.md "Sharing the owner's machines' cores"): one core for
# single-core steps, at normal priority, the pool (6 of 8 cores while the owner
# keeps two) for the scaling steps and builds. MICROARCH_TITLE and MICROARCH_LINK
# name the session for `cores status`.
HOST=zen4-7940hs
NATIVE=znver4
VARIANTS=${VARIANTS:-"znver4 znver5 x86-64"}
SWEEP_VARIANTS=${SWEEP_VARIANTS:-"znver4"}
SELF_VARIANTS=${SELF_VARIANTS:-"znver4 znver5"}
TD_WIDTH=6
TD_RESYNC=resyncs_or_nc_redirects
SCALING_LANES="1 2 4 6"
FANOUT_WORKERS="1 2 4 6"
SWEEP_CHUNK=100
METRIC_CHUNK=60
PAUSE=${PAUSE:-0}
UNCORE=0

hold() { CORES_TITLE="${MICROARCH_TITLE:-microarch}" CORES_LINK="${MICROARCH_LINK:-}" cores -n 1 --priority normal -- "$@"; }
hold_multi() { CORES_TITLE="${MICROARCH_TITLE:-microarch}" CORES_LINK="${MICROARCH_LINK:-}" cores -n 6 --priority normal -- "$@"; }
hold_build() { CORES_TITLE="${MICROARCH_TITLE:-microarch}" CORES_LINK="${MICROARCH_LINK:-}" cores -n 6 -- "$@"; }
receipt() { echo "$(date -Is) cores grant CORES_CPUS=${CORES_CPUS:-unset} CORES=${CORES:-unset}"; }
