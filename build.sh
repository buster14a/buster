#!/usr/bin/env bash
set -euo pipefail
if [[ ${1-} == "test_all_combinations_ci" ]]; then
    if [[ "$(uname -s)" = "Linux" || "$(uname -s)" = "Darwin" ]]; then
        set -x
        tcc -v
        clang -v
        gcc -v
        zig version
    fi
fi

repository_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=tools/bootstrap_driver.sh
source "$repository_root/tools/bootstrap_driver.sh"
buster_bootstrap_driver "$repository_root" "$@"
