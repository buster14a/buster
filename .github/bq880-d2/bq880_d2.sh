#!/usr/bin/env bash
# bq880 D2: run the exact 425b8b5a post-base-build checks (binary_digest, lock_tree,
# frozen_receipt) against a base build produced inside a transient unit with the
# base-build stage's properties, once inside a unit with the outer unit's properties
# and once (on a copy) outside any sandbox. Scratch only: never touches the service,
# queue, lease, workspaces, results or dispatch. Run as root.
set -euo pipefail
REPO=${1:?usage: bq880_d2.sh /path/to/clean-425b8b5a-checkout SCRATCH_DIR}
D=${2:?usage: bq880_d2.sh /path/to/clean-425b8b5a-checkout SCRATCH_DIR}
BASE=ade6ac4b6ecb21f30b61b656439bac476c145e2f
test "$(git -C "$REPO" rev-parse HEAD)" = 425b8b5ac571a3320681f0394de61a82cbc42e5f
test -z "$(git -C "$REPO" status --porcelain)"
test ! -e "$D"
install -d -m 0755 -o root -g root "$D" "$D/bin"
W=$D/w
install -d -m 2710 -o buster-bench -g buster-bench-candidate "$W" "$W/base"
install -d -m 2750 -o buster-bench -g buster-bench-candidate "$W/base/source"
install -d -m 2700 -o buster-bench -g buster-bench-candidate "$W/base/build"
install -d -m 0710 -o buster-bench -g buster-bench "$W/results"
install -d -m 0700 -o buster-bench -g buster-bench "$W/results/job-9-attempt-10" "$D/outside-results"
cp -r "/opt/buster-bench/installed/sources/$BASE/." "$W/base/source/"
rm -f "$W/base/source/source.manifest"
chown -R buster-bench:buster-bench-candidate "$W/base/source"
find "$W/base/source" -type d -exec chmod 2750 {} +
find "$W/base/source" -type f -exec chmod 0440 {} +

# Harness = build.c@425b8b5a with its entry point renamed plus one appended function.
sed 's/^ProcessResult entry_point(void)$/ProcessResult buster_build_entry_point_unused(void)/' \
    "$REPO/build.c" > "$D/bin/bq_harness.c"
test "$(grep -c '^ProcessResult buster_build_entry_point_unused(void)$' "$D/bin/bq_harness.c")" = 1
cat >> "$D/bin/bq_harness.c" <<'EOF'

/* bq880 D2 harness: calls the exact 425b8b5a post-base-build checks. */
ProcessResult entry_point(void)
{
    char const* build = getenv("BQH_BUILD");
    char const* result = getenv("BQH_RESULT");
    bool ok = build && result;
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    BenchServiceRecipeManifest manifest = {.result_directory = -1, .base_build_directory = -1,
                                           .candidate_build_directory = -1};
    if (ok)
    {
        manifest.job_id = S8("9");
        manifest.attempt_token = S8("10");
        manifest.base_revision = S8("ade6ac4b6ecb21f30b61b656439bac476c145e2f");
        manifest.candidate_revision = S8("425b8b5ac571a3320681f0394de61a82cbc42e5f");
        manifest.base_build = string_from_pointer(build);
        manifest.result_root = string_from_pointer(result);
        manifest.result_directory = bench_service_recipe_open_directory(manifest.result_root);
        manifest.base_build_directory = bench_service_recipe_open_directory(manifest.base_build);
        struct stat info = {0};
        ok = manifest.result_directory >= 0 && manifest.base_build_directory >= 0 &&
             fstat(manifest.result_directory, &info) == 0;
        manifest.result_device = info.st_dev;
        manifest.result_inode = info.st_ino;
        printf("BQH open ok=%d result_fd=%d build_fd=%d errno=%d\n", ok, manifest.result_directory,
               manifest.base_build_directory, errno);
    }
    errno = 0;
    bool digest = ok && bench_service_recipe_binary_digest(manifest.base_build_directory, manifest.base_digest);
    printf("BQH binary_digest ok=%d errno=%d sha256=%s\n", digest, errno, manifest.base_digest);
    errno = 0;
    bool lock = digest && bench_service_recipe_lock_tree(arena, manifest.base_build_directory, true);
    printf("BQH lock_tree ok=%d errno=%d\n", lock, errno);
    errno = 0;
    bool receipt = lock && bench_service_recipe_frozen_receipt(arena, &manifest, S8("base-build"));
    printf("BQH frozen_receipt ok=%d errno=%d\n", receipt, errno);
    fflush(stdout);
    return digest && lock && receipt ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
EOF
(cd "$REPO" && clang -I. -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv \
    -fno-strict-aliasing -funsigned-char -g "$D/bin/bq_harness.c" -o "$D/bin/bq_harness")
chmod 0755 "$D/bin/bq_harness"
sha256sum "$D/bin/bq_harness.c" "$D/bin/bq_harness"

COMMON=(-p KillMode=control-group -p SendSIGKILL=yes -p TimeoutStopSec=10s -p NoNewPrivileges=yes
  -p PrivateTmp=yes -p PrivateDevices=yes -p ProtectSystem=strict -p RestrictSUIDSGID=yes -p ProtectHome=yes
  -p ProtectControlGroups=yes -p ProtectKernelTunables=yes -p ProtectKernelModules=yes -p ProtectKernelLogs=yes
  -p ProtectClock=yes -p ProtectHostname=yes -p ProtectProc=invisible -p LockPersonality=yes
  -p MemoryDenyWriteExecute=yes -p RemoveIPC=yes -p KeyringMode=private -p RestrictNamespaces=yes
  -p RestrictRealtime=yes -p CapabilityBoundingSet= -p AmbientCapabilities= -p RestrictAddressFamilies=AF_UNIX
  -p SystemCallArchitectures=native -p SystemCallFilter=@system-service -p SystemCallErrorNumber=EPERM
  -p AllowedCPUs=2 -p MemoryMax=8589934592 -p MemorySwapMax=0 -p TasksMax=256 -p PrivateNetwork=yes)
STAGE=(--quiet --wait --pipe --collect --service-type=exec --setenv=PATH=/usr/bin:/bin --setenv=LC_ALL=C
  --uid=buster-bench --gid=buster-bench -p UMask=0077 "${COMMON[@]}"
  -p "ReadOnlyPaths=$W/base/source" -p "ReadWritePaths=$W/base/build" "--working-directory=$W/base/source")
DRIVER=/usr/local/libexec/buster-bench-build
echo "D2 build-root before: $(stat -c '%d %i %a %U:%G' "$W/base/build")"
rc=0; systemd-run --unit=bq880-d2-base-generate "${STAGE[@]}" "$DRIVER" generate --build-directory "$W/base/build" \
  --config Release --cc clang --no-include-tests --no-developer-targets --no-check-optional-warnings --no-fuzz \
  --no-sanitize --no-time-trace --no-instrument --no-lto > "$D/generate.log" 2>&1 || rc=$?
echo "D2 generate exit=$rc"
rc=0; systemd-run --unit=bq880-d2-base-build "${STAGE[@]}" "$DRIVER" build --build-directory "$W/base/build" \
  --config Release -t ide -- -j1 > "$D/build.log" 2>&1 || rc=$?
echo "D2 build exit=$rc"
echo "D2 build-root after: $(stat -c '%d %i %a %U:%G' "$W/base/build")"
tail -n 2 "$D/generate.log" "$D/build.log"

# Copy for the outside-sandbox control before the sandboxed lock_tree mutates modes.
cp -a "$W/base/build" "$D/outside-build"
chown -R buster-bench:buster-bench-candidate "$D/outside-build"
find "$W/base/build" \( -type l -o -type p -o -type s -o \( -type f -links +1 \) -o -size +128M \) -ls

rc=0; systemd-run --unit=bq880-d2-outer --quiet --wait --pipe --collect --service-type=exec \
  --setenv=PATH=/usr/bin:/bin --setenv=LC_ALL=C "--setenv=BQH_BUILD=$W/base/build" \
  "--setenv=BQH_RESULT=$W/results/job-9-attempt-10" --uid=buster-bench --gid=buster-bench -p UMask=0077 \
  "${COMMON[@]}" -p "InaccessiblePaths=/var/lib/buster-bench/queue /var/lib/buster-bench/lease" \
  -p "ReadOnlyPaths=/opt/buster-bench/installed" -p "ReadWritePaths=$W" "$D/bin/bq_harness" || rc=$?
echo "D2 sandboxed harness exit=$rc"
rc=0; setpriv --reuid=buster-bench --regid=buster-bench --init-groups --inh-caps=-all -- \
  env "BQH_BUILD=$D/outside-build" "BQH_RESULT=$D/outside-results" "$D/bin/bq_harness" || rc=$?
echo "D2 outside harness exit=$rc"
ls -la "$W/results/job-9-attempt-10" "$D/outside-results"
