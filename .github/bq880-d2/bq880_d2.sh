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
test "$(git -C "$REPO" rev-parse HEAD)" = "${BQ_D2_EXPECT:-425b8b5ac571a3320681f0394de61a82cbc42e5f}"
test -z "$(git -C "$REPO" status --porcelain)"
test ! -e "$D"
case "$D" in /tmp/*|/var/tmp/*) echo "SCRATCH_DIR must not be under /tmp or /var/tmp (PrivateTmp hides them)"; exit 2;; esac
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

/* bq880 D2 harness: calls the exact 425b8b5a post-base-build checks.  With
 * BQH_WAIT it pins both directory fds first and waits for that file, like the
 * real outer unit, which opens them at prepare time before base-generate. */
static void bqh_diagnose(char const* build, bool check_mode)
{
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    gid_t cgid = candidate ? candidate->pw_gid : (gid_t)-1;
    char stack[256][1100];
    int depth = 0, nodes = 0, bad = 0;
    snprintf(stack[depth++], sizeof(stack[0]), "%s", "");
    printf("BQH diagnose euid=%u egid=%u candidate_gid=%d\n", (unsigned)geteuid(), (unsigned)getegid(), (int)cgid);
    while (depth > 0)
    {
        char rel[1100];
        snprintf(rel, sizeof(rel), "%s", stack[--depth]);
        char dir[3000];
        snprintf(dir, sizeof(dir), "%s%s%s", build, rel[0] ? "/" : "", rel);
        DIR* d = opendir(dir);
        if (!d) { printf("BQH node %s opendir errno=%d\n", rel[0] ? rel : ".", errno); bad += 1; continue; }
        struct dirent* e;
        while ((e = readdir(d)))
        {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char r[1100], full[3200];
            snprintf(r, sizeof(r), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
            snprintf(full, sizeof(full), "%s/%s", build, r);
            struct stat a = {0}, b = {0};
            int sa = lstat(full, &a);
            usleep(2000);
            int sb = lstat(full, &b);
            nodes += 1;
            bool isdir = S_ISDIR(a.st_mode), isreg = S_ISREG(a.st_mode);
            unsigned want = isdir ? 0550u : (a.st_mode & 0111) ? 0550u : 0440u;
            bool typeok = isdir || (isreg && a.st_nlink == 1);
            bool uidok = a.st_uid == 0 || a.st_uid == geteuid();
            bool gidok = a.st_gid == 0 || a.st_gid == getegid() || a.st_gid == cgid;
            bool modeok = !check_mode || (a.st_mode & 07777) == want;
            bool sizeok = isdir || (u64)a.st_size <= BENCH_SERVICE_RECIPE_RECEIPT_FILE_CAP;
            bool stable = sa == 0 && sb == 0 && bench_service_recipe_receipt_same(&a, &b);
            if (!(typeok && uidok && gidok && modeok && sizeok && stable && strlen(r) <= 1024))
            {
                bad += 1;
                printf("BQH node %s type=%o nlink=%lu uid=%u gid=%u mode=%o size=%lld typeok=%d uidok=%d gidok=%d modeok=%d sizeok=%d stable=%d mtime=%lld.%09ld ctime=%lld.%09ld\n",
                       r, (unsigned)(a.st_mode & S_IFMT), (unsigned long)a.st_nlink, (unsigned)a.st_uid, (unsigned)a.st_gid,
                       (unsigned)(a.st_mode & 07777), (long long)a.st_size, typeok, uidok, gidok, modeok, sizeok, stable,
                       (long long)a.st_mtim.tv_sec, a.st_mtim.tv_nsec, (long long)a.st_ctim.tv_sec, a.st_ctim.tv_nsec);
            }
            if (isdir && depth < 256) snprintf(stack[depth++], sizeof(stack[0]), "%s", r);
        }
        closedir(d);
    }
    printf("BQH diagnose nodes=%d flagged=%d\n", nodes, bad);
}

ProcessResult entry_point(void)
{
    char const* build = getenv("BQH_BUILD");
    char const* result = getenv("BQH_RESULT");
    char const* wait_for = getenv("BQH_WAIT");
    bool ok = build && result;
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    BenchServiceRecipeManifest manifest = {.result_directory = -1, .base_build_directory = -1,
                                           .candidate_build_directory = -1};
    struct stat pinned = {0};
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
             fstat(manifest.result_directory, &info) == 0 && fstat(manifest.base_build_directory, &pinned) == 0;
        manifest.result_device = info.st_dev;
        manifest.result_inode = info.st_ino;
        printf("BQH open ok=%d pinned=%llu:%llu errno=%d\n", ok, (unsigned long long)pinned.st_dev,
               (unsigned long long)pinned.st_ino, errno);
        fflush(stdout);
    }
    if (ok && wait_for)
    {
        struct stat go = {0};
        for (int tick = 0; stat(wait_for, &go) != 0 && tick < 36000; tick += 1) usleep(100000);
        struct stat now = {0}, path_now = {0};
        fstat(manifest.base_build_directory, &now);
        stat(build, &path_now);
        printf("BQH after-wait pinned_fd=%llu:%llu nlink=%lu path=%llu:%llu\n", (unsigned long long)now.st_dev,
               (unsigned long long)now.st_ino, (unsigned long)now.st_nlink, (unsigned long long)path_now.st_dev,
               (unsigned long long)path_now.st_ino);
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
    if (ok && !(digest && lock && receipt)) bqh_diagnose(build, lock);
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
OUTER=(--quiet --wait --pipe --collect --service-type=exec --setenv=PATH=/usr/bin:/bin --setenv=LC_ALL=C
  --uid=buster-bench --gid=buster-bench -p UMask=0077 "${COMMON[@]}"
  -p "InaccessiblePaths=-/var/lib/buster-bench/queue -/var/lib/buster-bench/lease"
  -p "ReadOnlyPaths=/opt/buster-bench/installed" -p "ReadWritePaths=$W")
DRIVER=/usr/local/libexec/buster-bench-build
GO=$W/results/go
echo "D2 build-root before: $(stat -c '%d %i %a %U:%G' "$W/base/build")"
# 1. Pinned outer: opens both fds now (like prepare), waits for $GO, then runs the checks.
systemd-run --unit=bq880-d2-outer-pinned "${OUTER[@]}" "--setenv=BQH_BUILD=$W/base/build" \
  "--setenv=BQH_RESULT=$W/results/job-9-attempt-10" "--setenv=BQH_WAIT=$GO" "$D/bin/bq_harness" \
  < /dev/null > "$D/outer-pinned.log" 2>&1 & PINNED=$!
for i in $(seq 100); do grep -q '^BQH open' "$D/outer-pinned.log" 2>/dev/null && break; sleep 0.1; done
cat "$D/outer-pinned.log"
# 2. Stages, as the broker launches them.
rc=0; systemd-run --unit=bq880-d2-base-generate "${STAGE[@]}" "$DRIVER" generate --build-directory "$W/base/build" \
  --config Release --cc clang --no-include-tests --no-developer-targets --no-check-optional-warnings --no-fuzz \
  --no-sanitize --no-time-trace --no-instrument --no-lto > "$D/generate.log" 2>&1 || rc=$?
echo "D2 generate exit=$rc"
rc=0; systemd-run --unit=bq880-d2-base-build "${STAGE[@]}" "$DRIVER" build --build-directory "$W/base/build" \
  --config Release -t ide -- -j1 > "$D/build.log" 2>&1 || rc=$?
echo "D2 build exit=$rc"
echo "D2 build-root after: $(stat -c '%d %i %a %U:%G' "$W/base/build")"
tail -n 2 "$D/generate.log" "$D/build.log"
if [ "$rc" != 0 ] || [ ! -f "$W/base/build/Release/ide" ]; then touch "$GO"; wait "$PINNED" || true; cat "$D/outer-pinned.log"; echo "D2 STOP: stage build failed"; exit 3; fi
# Controls are copied before the pinned run mutates modes.
cp -a "$W/base/build" "$D/outside-build"
chown -R buster-bench:buster-bench-candidate "$D/outside-build"
find "$W/base/build" \( -type l -o -type p -o -type s -o \( -type f -links +1 \) -o -size +128M \) -ls
# 3. Release the pinned outer (0.1-0.4 s after the stage, as in N2).
touch "$GO"
rc=0; wait "$PINNED" || rc=$?
cat "$D/outer-pinned.log"
echo "D2 pinned sandboxed harness exit=$rc"
rm -f "$GO"
# 4. Control: same checks outside any sandbox on the copy.
rc=0; setpriv --reuid=buster-bench --regid=buster-bench --init-groups --inh-caps=-all -- \
  env "BQH_BUILD=$D/outside-build" "BQH_RESULT=$D/outside-results" "$D/bin/bq_harness" || rc=$?
echo "D2 outside harness exit=$rc"
ls -la "$W/results/job-9-attempt-10" "$D/outside-results"
