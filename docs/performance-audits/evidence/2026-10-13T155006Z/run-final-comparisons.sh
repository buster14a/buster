#!/bin/sh
set -eu
cd /home/david/Documents/Codex/2026-10-13/ana/work/buster
unset BUSTER_TEST_JOBS BUSTER_TEST_RETAIN_IMAGES
experiment_root=/home/david/Documents/Codex/2026-10-13/ana/work
baseline_revision=56221853cc5b11acb33e10ceca29484f7560bcf6
candidate_revision=$(git rev-parse HEAD)
# This host's perf emits instructions:u for the unqualified request. Retain
# and check that actual scope; qualified requests hit reported parser bug #4123.
"$experiment_root/performance-evidence" compare --claim work-reduction --tool perf-stat \
    --baseline "$experiment_root/frozen/ide-baseline" --candidate "$experiment_root/frozen/ide-candidate-v2" \
    --baseline-revision "$baseline_revision" --candidate-revision "$candidate_revision" \
    --events instructions --out "$experiment_root/ab-os-instructions-v2" --runs 6 --warmup 1 \
    -- test --verbose=0 --module=os_tests
"$experiment_root/performance-evidence" compare --claim native-runtime --tool time \
    --baseline "$experiment_root/frozen/ide-baseline" --candidate "$experiment_root/frozen/ide-candidate-v2" \
    --baseline-revision "$baseline_revision" --candidate-revision "$candidate_revision" \
    --out "$experiment_root/ab-suite" --runs 6 --warmup 0 -- test --verbose=0
