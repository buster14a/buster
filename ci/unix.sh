#!/usr/bin/env bash
set -euox pipefail

BUSTER_BUILD_DIRECTORY=build_ci-cc_${BUSTER_CC}-optimize_${BUSTER_OPTIMIZE}-sanitize_${BUSTER_SANITIZE}-fuzz_${BUSTER_FUZZ}-unity_build_${BUSTER_UNITY_BUILD}
source ./generate.sh
source ./build.sh test_all
