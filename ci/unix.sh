#!/usr/bin/env bash
set -euox pipefail

BUSTER_CC_DIRECTORY=${BUSTER_CC//[^A-Za-z0-9_.-]/_}
BUSTER_BUILD_DIRECTORY="build_ci-cc_${BUSTER_CC_DIRECTORY}-optimize_${BUSTER_OPTIMIZE}-sanitize_${BUSTER_SANITIZE}-fuzz_${BUSTER_FUZZ}"
./generate.py \
    --build-directory "$BUSTER_BUILD_DIRECTORY" \
    --cc "$BUSTER_CC" \
    --ci "$BUSTER_CI" \
    --fuzz "$BUSTER_FUZZ" \
    --optimize "$BUSTER_OPTIMIZE" \
    --sanitize "$BUSTER_SANITIZE"
./build.py --build-directory "$BUSTER_BUILD_DIRECTORY" --verbose test_all
