#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/bin:/bin
task_root=/home/david/buster-fast-bench/codex-117-01a16812
bundle=/home/david/buster-fast-bench/preprocessor-corpora-astra-01a158dd
date -u +'%Y-%m-%dT%H:%M:%SZ'
uname -a
grep -m1 '^model name' /proc/cpuinfo
grep -q 'AMD Ryzen 7 9700X' /proc/cpuinfo
grep '^Cpus_allowed_list:' /proc/self/status
printf 'CPU 2 governor: '
cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor
printf 'CPU 2 energy preference: '
cat /sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference
/usr/bin/clang --version
test "$(command -v clang)" = /usr/bin/clang
printf 'PATH=%s; resolved clang=%s\n' "$PATH" "$(command -v clang)"
printf '%s  %s\n' \
    939a882527432ec23b094c289e7f170bf2d6ec282e74dde75e31b08602fa3eae /usr/bin/clang \
    172ec00f7d0de882c93da42211595f9b00590c26be66836d01e948b37c40e841 "$task_root/candidate-9b70ee7.tar" \
    153fd8b801124f235e05b988643b5b971f0aa3c86a3cb128d693122dbc2a8d7e "$task_root/baseline-21d632c.tar" \
    8faf61f7982ef6272cab79ec25554f6723e55323f85091222a586bed1b433acc "$task_root/base-635434f2.tar" \
    391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd "$bundle/MANIFESTS.sha256" | sha256sum --check
for snapshot in candidate-9b70ee7 baseline-21d632c base-635434f2; do
    manifest="$task_root/source-identities/$snapshot.sha256"
    sha256sum "$manifest" "$task_root/source-identities/$snapshot.files"
    (cd "$task_root/$snapshot" && sha256sum --check --status "$manifest")
    diff -u "$task_root/source-identities/$snapshot.files" \
        <(cd "$task_root/$snapshot" && find . -path ./build -prune -o -type f -print | LC_ALL=C sort)
    test -z "$(cd "$task_root/$snapshot" && find . -path ./build -prune -o ! -type d ! -type f -print -quit)"
    printf '%s: every source byte and the inventory match the frozen archive manifest\n' "$snapshot"
done
sha256sum --check --status "$bundle/MANIFESTS.sha256"
for entry in headers:headers-bundle libc:libc sqlite:sqlite musl-generated:musl-generated repository:repository config:config; do
    task_directory="$bundle/${entry%:*}"
    manifest="$bundle/manifests/${entry#*:}.sha256"
    (cd "$task_directory" && sha256sum --check --status "$manifest")
    actual="$(find "$task_directory" -type f -printf '.\n' | wc -l)"
    expected="$(wc -l < "$manifest")"
    test "$actual" -eq "$expected"
    test -z "$(find "$task_directory" ! -type d ! -type f -print -quit)"
    printf '%s: %s files verified, no extra/nonregular entries\n' "${entry%:*}" "$actual"
done
(cd "$bundle/headers/usr" && sha256sum --check --status "$bundle/manifests/headers.sha256")
sha256sum \
    "$task_root/candidate-9b70ee7/build/compiler" \
    "$task_root/baseline-21d632c/build/compiler" \
    "$task_root/candidate-9b70ee7/build/compile_commands.json" \
    "$task_root/baseline-21d632c/build/compile_commands.json" \
    "$task_root/base-635434f2/src/buster/compiler/main.c" \
    "$task_root/117-implementation-9b70ee7-c.txt" \
    "$bundle/sqlite/sqlite3.c" \
    "$bundle/config/"*.args \
    "$bundle/config/"*-c.txt "$bundle/config/libc-ch.txt" "$bundle/config/"musl-*.txt
printf 'All pinned manifest identities and bytes verified.\n'
