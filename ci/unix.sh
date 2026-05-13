#!/usr/bin/env bash
set -euox pipefail

BUSTER_BUILD_DIRECTORY=build_ci-cc_${BUSTER_CC}-optimize_${BUSTER_OPTIMIZE}-sanitize_${BUSTER_SANITIZE}-fuzz_${BUSTER_FUZZ}
source ./generate.sh
source ./build.sh --verbose test_all
