#!/usr/bin/env bash
set -euox pipefail

BUSTER_CC_DIRECTORY=${BUSTER_CC//[^A-Za-z0-9_.-]/_}
BUSTER_BUILD_DIRECTORY="build_ci-cc_${BUSTER_CC_DIRECTORY}-optimize_${BUSTER_OPTIMIZE}-sanitize_${BUSTER_SANITIZE}-fuzz_${BUSTER_FUZZ}"
source ./generate.sh
source ./build.sh --verbose test_all
