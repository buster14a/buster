#!/usr/bin/env bash
# Temporary, isolated #1162 validation transport. Never install on a protected host.
set -Eeuo pipefail
subject=57765f220fae6b4073aa3947b36b9fd605cf2a5b
subject_tree=eb3163d1f4517661ea86ba8ad5dca611dad28485
subject_build_blob=a016a487b745357883a5cd76c9c2712485c01d7c
baseline=ade6ac4b6ecb21f30b61b656439bac476c145e2f
evidence="${RUNNER_TEMP:?}/issue1162-exact-slice-evidence"
source_root="$RUNNER_TEMP/issue1162-source"
repo_root="$(git rev-parse --show-toplevel)"
live_probe_helper="$repo_root/.github/scripts/issue1162_live_probe.py"
stage_observer_helper="$repo_root/.github/scripts/issue1162_stage_observer.py"
workspace_observer_helper="$repo_root/.github/scripts/issue1162_workspace_observer.py"
manager_denial_helper="$repo_root/.github/scripts/issue1162_manager_denial_probe.py"
broker_observer_helper="$repo_root/.github/scripts/issue1162_broker_observer.py"
broker_evidence_helper="$repo_root/.github/scripts/issue1162_broker_evidence.py"
broker_entry_helper="$repo_root/.github/scripts/issue1162_broker_entry_evidence.py"
frozen_tree_helper="$repo_root/.github/scripts/issue1162_frozen_tree_evidence.py"
payload="$RUNNER_TEMP/issue1162-install"
guest="issue1162-exact-${GITHUB_RUN_ID:?}"
image="issue1162-exact:${GITHUB_RUN_ID}"
mkdir -p "$evidence" "$payload/binaries" "$payload/sources" "$payload/units"
chmod 0700 "$evidence"
exec > >(tee "$evidence/run.log") 2>&1
python3 "$live_probe_helper" --lease-self-test | tee "$evidence/lease-probe-self-test.txt"
python3 "$live_probe_helper" --self-test | tee "$evidence/live-probe-self-test.txt"
python3 "$stage_observer_helper" --self-test | tee "$evidence/stage-observer-self-test.txt"
python3 "$workspace_observer_helper" --self-test | tee "$evidence/workspace-observer-self-test.txt"
python3 "$manager_denial_helper" --self-test | tee "$evidence/manager-denial-self-test.txt"
python3 "$broker_observer_helper" --self-test | tee "$evidence/broker-observer-self-test.txt"
python3 "$broker_evidence_helper" --self-test | tee "$evidence/broker-evidence-self-test.txt"
python3 "$broker_entry_helper" --self-test | tee "$evidence/broker-entry-evidence-self-test.txt"
python3 "$frozen_tree_helper" self-test | tee "$evidence/frozen-tree-self-test.txt"
python3 "$repo_root/.github/scripts/issue1162_ancestor_preflight_test.py" | tee "$evidence/ancestor-preflight-self-test.txt"
observer_pid=
observer_collected=false
broker_observer_pid=
broker_observer_collected=false
broker_observer_status=1
broker_journal_collected=false
capture_broker_journal() {
  if [[ "$broker_journal_collected" != true ]]; then
    local sync_status=0
    timeout --signal=TERM --kill-after=2 15 sudo docker exec "$guest" journalctl --sync \
      >"$evidence/broker-journal-sync.txt" 2>&1 || sync_status=$?
    local capture_codes
    if timeout --signal=TERM --kill-after=2 30 sudo docker exec "$guest" \
      timeout 25 journalctl --no-pager --all -b -o json -u 'buster-bench-systemd-broker@*.service' \
      2>"$evidence/broker-journal-stderr.txt" | head -c 134217729 >"$evidence/broker-journal.jsonl"; then
      capture_codes=("${PIPESTATUS[@]}")
    else
      capture_codes=("${PIPESTATUS[@]}")
    fi
    python3 - "$evidence" "$sync_status" "${capture_codes[@]}" <<'JOURNAL_CAPTURE' || return 1
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
raw = (root / "broker-journal.jsonl").read_bytes()
statuses = [int(value) for value in sys.argv[2:]]
complete = len(statuses) == 3 and all(value == 0 for value in statuses) and len(raw) <= 128 * 1024 * 1024
record = {"complete": complete, "sha256": hashlib.sha256(raw).hexdigest(),
          "bytes": len(raw), "records": len(raw.splitlines()), "command_statuses": statuses}
(root / "broker-journal-capture.json").write_text(json.dumps(record, sort_keys=True) + "\n")
JOURNAL_CAPTURE
    broker_journal_collected=true
  fi
}
collect_broker_observer() {
  if [[ -n "$broker_observer_pid" && "$broker_observer_collected" != true ]]; then
    printf 'snapshot=%s\n' "${1:-completed}" >"$evidence/broker-observer-retention.txt"
    local stop_requested=true
    timeout --signal=TERM --kill-after=2 10 sudo docker exec "$guest" python3 -c '
import os
p="/root/issue1162-install/broker-observer-output/stop"
try:
    fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    os.close(fd)
except FileExistsError:
    pass
' || stop_requested=false
    local stop_deadline=$((SECONDS + 35))
    while [[ "$stop_requested" == true ]] && kill -0 "$broker_observer_pid" 2>/dev/null && (( SECONDS < stop_deadline )); do sleep 1; done
    if kill -0 "$broker_observer_pid" 2>/dev/null; then
      echo "BROKER_OBSERVER_STOP_INCOMPLETE requested=$stop_requested"
      kill -TERM "$broker_observer_pid" 2>/dev/null || true
      broker_observer_status=124
    else
      if wait "$broker_observer_pid"; then broker_observer_status=0; else broker_observer_status=$?; fi
    fi
    printf 'exit_status=%s\n' "$broker_observer_status" >"$evidence/broker-observer-exit.txt"
    mkdir -p "$evidence/broker-observer-artifacts"
    if timeout --signal=TERM --kill-after=2 25 sudo docker cp "$guest:/root/issue1162-install/broker-observer-output/." "$evidence/broker-observer-artifacts" &&
       timeout --signal=TERM --kill-after=2 10 sudo chown -R -- "$(id -u):$(id -g)" "$evidence/broker-observer-artifacts"; then
      broker_observer_collected=true
    else
      echo "BROKER_OBSERVER_ARTIFACT_COPY_FAILED"
      return 1
    fi
  fi
}
collect_stage_observer() {
  if [[ -n "$observer_pid" && "$observer_collected" != true ]]; then
    # A failure-path snapshot may overlap the observer's last writes and is
    # retained as partial evidence, never as a completed observation.
    printf 'snapshot=%s\n' "${1:-completed}" >"$evidence/stage-observer-retention.txt"
    mkdir -p "$evidence/stage-observer-artifacts"
    if sudo docker cp "$guest:/root/issue1162-install/stage-observer-output/." "$evidence/stage-observer-artifacts" &&
       sudo chown -R -- "$(id -u):$(id -g)" "$evidence/stage-observer-artifacts"; then
      observer_collected=true
    else
      echo "STAGE_OBSERVER_ARTIFACT_COPY_FAILED"
      return 1
    fi
  fi
}
retain() {
  original_result=$?
  result=$original_result
  trap - EXIT
  set +e
  echo "RETAIN original_exit=$result"
  if sudo docker inspect "$guest" >/dev/null 2>&1; then
    # Preserve partial observer diagnostics even when RESULT/export failed.
    # The observer is read-only and its own deadline is submit-relative.
    if ! collect_stage_observer partial; then
      if (( result == 0 )); then result=1; fi
    fi
    if ! collect_broker_observer partial; then
      if (( result == 0 )); then result=1; fi
    fi
    if ! capture_broker_journal; then
      if (( result == 0 )); then result=1; fi
    fi
    sudo docker inspect "$guest" >"$evidence/container.json" 2>&1
    sudo docker logs "$guest" >"$evidence/container.log" 2>&1
    sudo docker exec "$guest" journalctl --no-pager -b -u buster-bench.service -u 'buster-bench-*.service' -u 'buster-bench-systemd-broker@*.service' >"$evidence/journal.log" 2>&1
    sudo docker exec "$guest" systemctl list-units --all 'buster-bench*' >"$evidence/units-final.txt" 2>&1
    sudo docker exec "$guest" sh -c 'find /var/lib/buster-bench -xdev -printf "%m %u:%g %s %p\n" | sort' >"$evidence/state-inventory.txt" 2>&1
    sudo docker exec "$guest" sh -c 'stat -c "lease device=%d inode=%i links=%h mode=%a owner=%u:%g" /var/lib/buster-bench/lease/host.lock' >"$evidence/lease-final.txt" 2>&1
    broker="$(sudo docker exec "$guest" systemctl list-units --all --plain --no-legend 'buster-bench-systemd-broker@*.service' | awk '{print $1; exit}')"
    if [[ -n "$broker" ]]; then
      sudo docker exec "$guest" systemctl show "$broker" -p Id -p User -p Group -p SupplementaryGroups -p MainPID -p ExecMainStatus -p Result -p InvocationID >"$evidence/broker-effective.txt" 2>&1
    fi
    attempt="$(sed -nE 's/^job=[0-9]+ token=([0-9]+) .*/\1/p' "$evidence/status-latest.txt" 2>/dev/null | head -1)"
    if [[ -n "${job:-}" && -n "$attempt" && "$attempt" != 0 ]]; then
      sudo docker exec "$guest" /root/issue1162-install/broker-state-probe "$job" "$attempt" "$baseline" "$subject" >"$evidence/broker-state-probe.txt" 2>&1
    fi
    sudo docker exec "$guest" tar -C /var/lib -czf - buster-bench >"$evidence/retained-state.tgz" 2>"$evidence/state-tar.log"
    tar -tzf "$evidence/retained-state.tgz" >"$evidence/state-tar-inventory.txt" 2>>"$evidence/state-tar.log"
    sha256sum "$evidence/retained-state.tgz" >"$evidence/state-tar-sha256.txt"
    sudo docker rm -f "$guest" >"$evidence/container-removal.txt" 2>&1
    if sudo docker ps -a --format '{{.Names}}' | grep -Fx "$guest"; then result=1; fi
  fi
  echo "FINAL original_exit=$original_result final_exit=$result" | tee "$evidence/final.txt"
  exit "$result"
}
trap retain EXIT
printf 'SUBJECT_SHA=%s BASE_SHA=%s WORKFLOW_SHA=%s RUN=%s\n' "$subject" "$baseline" "$GITHUB_SHA" "$GITHUB_RUN_ID"
git rev-parse "$subject" "$subject^{tree}" "$baseline" "$baseline^{tree}" | tee "$evidence/revisions.txt"
test "$(git rev-parse "$subject^{tree}")" = "$subject_tree"
test "$(git rev-parse "$subject:build.c")" = "$subject_build_blob"
test "$(git rev-parse "$baseline^{tree}")" = 4c5306221fdb22fccc929b55e333163742de17d0
git worktree add --detach "$source_root" "$subject"
test -z "$(git -C "$source_root" status --porcelain=v1)"
git -C "$source_root" ls-tree -r -z --full-tree "$subject" >"$evidence/source-git-inventory.nul"
git -C "$source_root" show --no-patch --format=raw "$subject" >"$evidence/source-commit.txt"
for rev in "$baseline" "$subject"; do
  mkdir -p "$payload/sources/$rev"
  git archive "$rev" -- src cmake CMakeLists.txt | tar -x -C "$payload/sources/$rev"
  REV="$rev" ROOT="$payload/sources/$rev" python3 - <<'PY'
import hashlib, os
from pathlib import Path
root = Path(os.environ["ROOT"])
rev = os.environ["REV"]
files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix().encode())
assert all(not p.is_symlink() for p in root.rglob("*"))
lines = ["BQ-SOURCE-V1", "repository=buster14a/buster", "revision=" + rev]
for path in files:
    lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + " " + path.relative_to(root).as_posix())
manifest = ("\n".join(lines) + "\n").encode()
expected = {
    "ade6ac4b6ecb21f30b61b656439bac476c145e2f": (375, 42585, "ebf4a4b4e5943dc60dd9fd9d175af643fbe0d0eee72e70e245c688aaabcb3007"),
    "57765f220fae6b4073aa3947b36b9fd605cf2a5b": (377, 42835, "1199e6a4c0dceaebd7795d3c7ecb6183c4e21997a183c4f1dea8957170f526cc"),
}[rev]
actual = (len(files), len(manifest), hashlib.sha256(manifest).hexdigest())
print("SOURCE_CLOSURE", rev, "files", actual[0], "bytes", actual[1], "sha256", actual[2], flush=True)
assert actual == expected, (actual, expected)
(root / "source.manifest").write_bytes(manifest)
PY
  mkdir -p "$evidence/source-manifests"
  cp "$payload/sources/$rev/source.manifest" "$evidence/source-manifests/$rev.manifest"
  cmp "$payload/sources/$rev/source.manifest" "$evidence/source-manifests/$rev.manifest"
  sha256sum "$evidence/source-manifests/$rev.manifest"
done
cd "$source_root"
mkdir -p build
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o build/buster-bench-build
readelf -W -l build/buster-bench-build | tee "$evidence/build-driver-elf.txt"
grep -E 'GNU_STACK.*RW[[:space:]]' "$evidence/build-driver-elf.txt"
build/buster-bench-build bench_service capabilities
build/buster-bench-build bench_service self-test | tee "$evidence/service-self-test.txt"
build/buster-bench-build bench_service_broker self-test | tee "$evidence/broker-and-gate-self-test.txt"
clang -Isrc -DBUSTER_SINGLE_THREADED=1 -std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/throughput/throughput.c tools/throughput/shared.c -lm -o build/throughput
cat > "$payload/broker-state-probe.c" <<'PROBE'
#define main bq_original_broker_main
#include "tools/bench_service/systemd_broker.c"
#undef main
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = 1,
                               .operation = BQ_BROKER_START, .stage = BQ_BROKER_OUTER,
                               .job = strtoull(argv[1], NULL, 10), .attempt = strtoull(argv[2], NULL, 10)};
    snprintf(request.base, sizeof(request.base), "%s", argv[3]);
    snprintf(request.candidate, sizeof(request.candidate), "%s", argv[4]);
    struct passwd* service = getpwnam("buster-bench");
    uid_t uid = service ? service->pw_uid : (uid_t)-1;
    gid_t gid = service ? service->pw_gid : (gid_t)-1;
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    gid_t candidate_gid = candidate ? candidate->pw_gid : (gid_t)-1;
    BqBrokerPaths paths;
    bool paths_ok = bq_broker_paths(&request, &paths);
    printf("read-only probe of durable file predicates after failed attempt; separate root context\n");
    printf("request=%d paths=%d state=%d\n", bq_broker_request_valid(&request), paths_ok,
           paths_ok && bq_broker_state(&request, uid, gid, candidate_gid));
    if (!paths_ok) return 1;
    printf("private-root=%d workspace=%d results=%d result=%d queue=%d\n",
           bq_broker_private_directory("/var/lib/buster-bench", uid, candidate_gid, 0710),
           bq_broker_private_directory(BQ_BROKER_WORKSPACES, uid, candidate_gid, 02710),
           bq_broker_private_directory(BQ_BROKER_WORKSPACES "/results", uid, gid, 0710),
           bq_broker_private_directory(paths.result, uid, gid, 0700),
           bq_broker_private_directory(BQ_BROKER_QUEUE, uid, gid, 0710));
    printf("worker-record=%d lease-held=%d base-manifest=%d candidate-manifest=%d\n",
           bq_broker_worker_record(&request, &paths, uid, gid, false),
           bq_broker_lease_held(uid, gid), bq_broker_manifest(&request, &paths, uid, false),
           bq_broker_manifest(&request, &paths, uid, true));
    printf("installed-service=%d build=%d throughput=%d\n",
           bq_broker_installed_binary(BQ_BROKER_SERVICE, false), bq_broker_installed_binary(BQ_BROKER_BUILD, false),
           bq_broker_installed_binary(BQ_BROKER_THROUGHPUT, false));
    printf("installed-static-gate=%d\n", bq_broker_installed_binary(BQ_BROKER_GATE, true));
    return 0;
}
PROBE
clang -I "$source_root" -std=c11 -O0 -g -Wno-unused-function "$payload/broker-state-probe.c" -o "$payload/broker-state-probe"
install -m 0755 build/buster-bench-build "$payload/binaries/buster-bench-build"
install -m 0755 build/bench-service-tools/service "$payload/binaries/buster-bench-service"
install -m 0755 build/bench-service-tools/systemd-broker "$payload/binaries/buster-bench-systemd-broker"
install -m 0755 build/bench-service-tools/credential-gate "$payload/binaries/buster-bench-credential-gate"
readelf -W -l "$payload/binaries/buster-bench-credential-gate" | tee "$evidence/credential-gate-elf.txt"
if grep -Eq 'INTERP|DYNAMIC|GNU_STACK.*RWE' "$evidence/credential-gate-elf.txt"; then exit 1; fi
# The broker unit's first ExecStart: the static same-PID entry gate (#1162).
install -m 0755 build/bench-service-tools/broker-entry-gate "$payload/binaries/buster-bench-broker-entry-gate"
readelf -W -l "$payload/binaries/buster-bench-broker-entry-gate" | tee "$evidence/broker-entry-gate-elf.txt"
if grep -Eq 'INTERP|DYNAMIC|GNU_STACK.*RWE' "$evidence/broker-entry-gate-elf.txt"; then exit 1; fi
install -m 0755 build/bench-service-tools/systemd-broker-live-test "$payload/binaries/systemd-broker-live-test"
install -m 0755 build/throughput "$payload/binaries/buster-bench-throughput"
# The existing root-only cleanup regression owns fresh /tmp fixtures and is
# kept outside the installed service's executable namespace.
install -m 0755 build/bench-service-tools/service-tests "$payload/cleanup-identity-tests"
sha256sum "$payload/cleanup-identity-tests" | tee "$evidence/cleanup-identity-binary-sha256.txt"
cp tools/bench_service/deploy/{buster-bench.service,buster-bench.slice,buster-bench-systemd-broker.socket,buster-bench-systemd-broker@.service,buster-bench.tmpfiles.conf} "$payload/units/"
mkdir -p "$payload/recipes"
cp tools/bench_service/profiles/validate-buster-v1.recipe "$payload/recipes/"
sha256sum "$payload"/binaries/* "$payload"/units/* "$payload"/recipes/* | tee "$evidence/installed-sha256.txt"
cp "$live_probe_helper" "$payload/issue1162_live_probe.py"
cp "$stage_observer_helper" "$payload/issue1162_stage_observer.py"
cp "$workspace_observer_helper" "$payload/issue1162_workspace_observer.py"
cp "$manager_denial_helper" "$payload/issue1162_manager_denial_probe.py"
cp "$broker_observer_helper" "$payload/issue1162_broker_observer.py"
sha256sum "$live_probe_helper" "$payload/issue1162_live_probe.py" | tee "$evidence/live-probe-helper-sha256.txt"
sha256sum "$stage_observer_helper" "$payload/issue1162_stage_observer.py" | tee "$evidence/stage-observer-helper-sha256.txt"
sha256sum "$workspace_observer_helper" "$payload/issue1162_workspace_observer.py" | tee "$evidence/workspace-observer-helper-sha256.txt"
sha256sum "$manager_denial_helper" "$payload/issue1162_manager_denial_probe.py" | tee "$evidence/manager-denial-helper-sha256.txt"
sha256sum "$broker_observer_helper" "$payload/issue1162_broker_observer.py" | tee "$evidence/broker-observer-helper-sha256.txt"
sha256sum "$broker_evidence_helper" "$broker_entry_helper" "$frozen_tree_helper" | tee "$evidence/offline-evidence-helpers-sha256.txt"
clang --version | head -1 | tee "$evidence/toolchains.txt"
cmake --version | head -1 | tee -a "$evidence/toolchains.txt"
ninja --version | tee -a "$evidence/toolchains.txt"
for tool in clang cmake ninja; do
  tool_path="$(command -v "$tool")"
  printf 'tool=%s path=%s resolved=%s\n' "$tool" "$tool_path" "$(readlink -f "$tool_path")"
  sha256sum "$tool_path"
  ldd "$tool_path"
done >"$evidence/build-tool-dependencies.txt" 2>&1
for binary in "$payload"/binaries/* "$payload/cleanup-identity-tests"; do
  printf 'binary=%s\n' "$binary"
  if [[ "$binary" == "$payload/binaries/buster-bench-credential-gate" ||
        "$binary" == "$payload/binaries/buster-bench-broker-entry-gate" ]]; then
    printf 'static-gate: no dynamic loader or shared-library dependencies\n'
    readelf -W -l "$binary"
  else
    ldd "$binary"
  fi
done >"$evidence/installed-binary-dependencies.txt" 2>&1
printf 'PATH=%s\nLANG=%s\nLC_ALL=%s\n' "$PATH" "${LANG-}" "${LC_ALL-}" >"$evidence/build-environment.txt"
cat > "$payload/Dockerfile" <<'DOCKERFILE'
FROM ubuntu@sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv dbus util-linux python3 clang cmake ninja-build binutils build-essential git ca-certificates strace && rm -rf /var/lib/apt/lists/*
STOPSIGNAL SIGRTMIN+3
CMD ["/sbin/init"]
DOCKERFILE
cat > "$payload/provision.sh" <<'GUEST'
#!/bin/sh
set -eu
groupadd -g 65000 buster-bench
groupadd -g 65001 buster-bench-candidate
groupadd -g 65002 buster-github-runner
useradd -u 65000 -g 65000 -G 65001 -M -s /usr/sbin/nologin buster-bench
useradd -u 65001 -g 65001 -M -s /usr/sbin/nologin buster-bench-candidate
useradd -u 65002 -g 65002 -M -s /usr/sbin/nologin buster-github-runner
install -d -m 0755 /usr/local/libexec /etc/systemd/system /etc/buster-bench
cp /root/issue1162-install/binaries/* /usr/local/libexec/
chown root:root /usr/local/libexec/buster-bench-* /usr/local/libexec/systemd-broker-live-test
chmod 0755 /usr/local/libexec/buster-bench-* /usr/local/libexec/systemd-broker-live-test
cp /root/issue1162-install/units/*.service /root/issue1162-install/units/*.socket /root/issue1162-install/units/*.slice /etc/systemd/system/
chown root:root /etc/systemd/system/buster-bench*
chmod 0644 /etc/systemd/system/buster-bench*
cp /root/issue1162-install/units/buster-bench.tmpfiles.conf /etc/tmpfiles.d/issue1162-buster-bench.conf
systemd-tmpfiles --create /etc/tmpfiles.d/issue1162-buster-bench.conf
install -d -m 0710 -o buster-bench -g buster-bench /var/lib/buster-bench/workspaces/results
# GNU install and chmod preserve the parent's setgid bit on this filesystem.
# The installed results root must have exactly 0710 for the production worker.
chmod 0710 /var/lib/buster-bench/workspaces/results
chmod g-s /var/lib/buster-bench/workspaces/results
test "$(stat -c %a /var/lib/buster-bench/workspaces/results)" = 710
test "$(stat -c %u:%g /var/lib/buster-bench/workspaces/results)" = 65000:65000
install -d -m 0750 -o root -g buster-bench /opt/buster-bench
install -d -m 0550 -o root -g buster-bench /opt/buster-bench/installed
cp -a /root/issue1162-install/sources /opt/buster-bench/installed/
cp -a /root/issue1162-install/recipes /opt/buster-bench/installed/
chown -R root:buster-bench /opt/buster-bench/installed
find /opt/buster-bench/installed -type d -exec chmod 0550 {} +
find /opt/buster-bench/installed -type f -exec chmod 0440 {} +
chown buster-bench:buster-bench /var/lib/buster-bench/lease
touch /var/lib/buster-bench/lease/host.lock
chown buster-bench:buster-bench /var/lib/buster-bench/lease/host.lock
chmod 0640 /var/lib/buster-bench/lease/host.lock
printf 'device=%s\ninode=%s\n' \
  "$(stat -c %d /var/lib/buster-bench/lease/host.lock)" \
  "$(stat -c %i /var/lib/buster-bench/lease/host.lock)" \
  > /etc/buster-bench/systemd-broker-lease.identity
chown root:root /etc/buster-bench/systemd-broker-lease.identity
chmod 0444 /etc/buster-bench/systemd-broker-lease.identity
printf 'BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\ncandidate-uid=65001\ncandidate-gid=65001\nrunner-uid=65002\nrunner-gid=65002\n' > /etc/buster-bench/systemd-broker-accounts.identity
chown root:root /etc/buster-bench/systemd-broker-accounts.identity
chmod 0444 /etc/buster-bench/systemd-broker-accounts.identity
sha256sum /etc/buster-bench/systemd-broker-accounts.identity
cat /etc/buster-bench/systemd-broker-accounts.identity
# The broker checks its receipt parent as a root-owned, nonwritable directory.
chmod 0555 /etc/buster-bench
test "$(stat -c %a:%u:%g /etc/buster-bench)" = 555:0:0
test "$(stat -c %a:%u:%g /etc/buster-bench/systemd-broker-lease.identity)" = 444:0:0
test "$(stat -c %a:%u:%g /etc/buster-bench/systemd-broker-accounts.identity)" = 444:0:0
printf 'GUEST_IDENTITIES\n'; id buster-bench; id buster-bench-candidate; id buster-github-runner
stat -c 'LEASE_DEVICE=%d LEASE_INODE=%i MODE=%a OWNER=%u:%g LINKS=%h' /var/lib/buster-bench/lease/host.lock
stat -c '%a %u:%g %n' /var/lib/buster-bench /var/lib/buster-bench/queue /var/lib/buster-bench/workspaces /var/lib/buster-bench/lease /var/lib/buster-bench/workspaces/results /etc/buster-bench /etc/buster-bench/systemd-broker-lease.identity
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/buster-bench.service /etc/systemd/system/buster-bench-systemd-broker.socket /etc/systemd/system/buster-bench-systemd-broker@.service /etc/systemd/system/buster-bench.slice
for file in /root/issue1162-install/binaries/*; do
  installed="/usr/local/libexec/$(basename "$file")"
  cmp "$file" "$installed"
  sha256sum "$installed"
  stat -c '%a %u:%g %h %n' "$installed"
done
for file in /root/issue1162-install/units/*.service /root/issue1162-install/units/*.socket /root/issue1162-install/units/*.slice; do
  installed="/etc/systemd/system/$(basename "$file")"
  cmp "$file" "$installed"
  sha256sum "$installed"
  stat -c '%a %u:%g %h %n' "$installed"
done
cmp /root/issue1162-install/recipes/validate-buster-v1.recipe /opt/buster-bench/installed/recipes/validate-buster-v1.recipe
sha256sum /opt/buster-bench/installed/recipes/validate-buster-v1.recipe
for file in /root/issue1162-install/sources/*/source.manifest; do
  revision="$(basename "$(dirname "$file")")"
  cmp "$file" "/opt/buster-bench/installed/sources/$revision/source.manifest"
  sha256sum "/opt/buster-bench/installed/sources/$revision/source.manifest"
done
printf 'GUEST_RUNTIME_TOOLCHAIN\n'
clang --version
cmake --version
ninja --version
dpkg-query -W -f='${Package} ${Version}\n'
for executable in /usr/local/libexec/buster-bench-* /usr/local/libexec/systemd-broker-live-test /root/issue1162-install/cleanup-identity-tests /usr/bin/clang /usr/bin/cmake /usr/bin/ninja; do
  printf 'runtime-executable=%s resolved=%s\n' "$executable" "$(readlink -f "$executable")"
  sha256sum "$executable"
  if [ "$executable" = /usr/local/libexec/buster-bench-credential-gate ] ||
     [ "$executable" = /usr/local/libexec/buster-bench-broker-entry-gate ]; then
    headers="$(readelf -W -l "$executable")"
    printf '%s\n' "$headers"
    if printf '%s\n' "$headers" | grep -Eq 'INTERP|DYNAMIC|GNU_STACK.*RWE'; then exit 1; fi
    printf 'static-gate: no dynamic loader or shared-library dependencies\n'
    dependencies=
  else
    dependencies="$(ldd "$executable")"
  fi
  printf '%s\n' "$dependencies"
  printf '%s\n' "$dependencies" | awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}' | while IFS= read -r library; do
    sha256sum "$library"
  done
done
GUEST
sudo docker build --pull --no-cache -t "$image" "$payload"
sudo docker run -d --name "$guest" --privileged --cgroup-parent=docker.slice --cgroupns=private --network none --env container=docker --tmpfs /tmp --tmpfs /run --tmpfs /run/lock "$image"
for attempt in $(seq 1 40); do
  if sudo docker exec "$guest" systemctl show --property=Version --property=SystemState >"$evidence/manager.txt" 2>&1; then break; fi
  sleep 1
done
cat "$evidence/manager.txt"
sudo docker exec "$guest" sh -ec 'test "$(cat /proc/1/comm)" = systemd; test "$(stat -fc %T /sys/fs/cgroup)" = cgroup2fs; test -w /sys/fs/cgroup/cgroup.subtree_control; grep -qw cpu /sys/fs/cgroup/cgroup.controllers; grep -qw cpuset /sys/fs/cgroup/cgroup.controllers; grep -qw memory /sys/fs/cgroup/cgroup.controllers; grep -qw pids /sys/fs/cgroup/cgroup.controllers'
sudo docker exec "$guest" sh -ec 'uname -a; systemd --version; cat /proc/sys/kernel/random/boot_id; cat /proc/self/cgroup; cat /proc/self/mountinfo; for name in cgroup.controllers cpuset.cpus.effective memory.max memory.swap.max pids.max; do printf "%s=" "$name"; cat "/sys/fs/cgroup/$name"; done' >"$evidence/guest-platform-and-ancestor.txt"
# Inspect the real host-visible guest ancestry as well as its private cgroup
# namespace, before copying or provisioning any service files.
guest_pid="$(sudo docker inspect --format '{{.State.Pid}}' "$guest")"
[[ "$guest_pid" =~ ^[1-9][0-9]*$ ]]
test "$(stat -fc %T /sys/fs/cgroup)" = cgroup2fs
sudo docker exec "$guest" stat -c '%d %i' /sys/fs/cgroup >"$evidence/guest-cgroup-root-before.txt"
read -r guest_cgroup_device guest_cgroup_inode guest_cgroup_extra <"$evidence/guest-cgroup-root-before.txt"
[[ "$guest_cgroup_device" =~ ^[1-9][0-9]*$ && "$guest_cgroup_inode" =~ ^[1-9][0-9]*$ && -z "$guest_cgroup_extra" ]]
sudo python3 - "$guest_pid" "$guest_cgroup_device" "$guest_cgroup_inode" <<'ANCESTORS' | tee "$evidence/host-visible-guest-ancestors.txt"
import json, os, re, sys
from pathlib import Path
pid = int(sys.argv[1])
namespace_root = (int(sys.argv[2]), int(sys.argv[3]))
proc = Path(f"/proc/{pid}")
before = (proc / "stat").read_text()
start = before[before.rfind(")") + 2:].split()[19]
cgroup = (proc / "cgroup").read_text()
assert cgroup.startswith("0::/") and cgroup.count("\n") == 1, cgroup
relative = cgroup[4:].strip()
parts = relative.split("/") if relative else []
assert len(parts) <= 64 and all(re.fullmatch(r"[A-Za-z0-9_.:@-]+", p) and p not in (".", "..") for p in parts)
root = Path("/sys/fs/cgroup")
assert (root.stat().st_dev, root.stat().st_ino) != namespace_root, "guest cgroup root must be private"
current = root
rows = []
namespace_found = False
for part in [None, *parts]:
    if part is not None:
        current = current / part
    assert not current.is_symlink() and current.is_dir(), current
    cpus = (current / "cpuset.cpus.effective").read_text().strip()
    assert len(cpus) <= 4096 and cpus, cpus
    ranges = [piece.split("-") for piece in cpus.split(",")]
    assert all(len(r) in (1, 2) and all(x.isdecimal() for x in r) and int(r[0]) <= int(r[-1]) for r in ranges)
    assert any(int(r[0]) <= 2 <= int(r[-1]) for r in ranges), (current, cpus)
    limits = {}
    for name, minimum in (("memory.max", 8 * 1024**3), ("pids.max", 256)):
        path = current / name
        # The cgroup-v2 root has no controller limit files.
        value = path.read_text().strip() if path.exists() else "root-unlimited"
        assert value != "root-unlimited" or current == root, (current, name)
        assert value in ("max", "root-unlimited") or (value.isdecimal() and int(value) >= minimum), (current, name, value)
        limits[name] = value
    swap = current / "memory.swap.max"
    limits["memory.swap.max"] = swap.read_text().strip() if swap.exists() else "unavailable"
    info = current.stat()
    rows.append({"path": str(current), "device": info.st_dev, "inode": info.st_ino,
                 "cpuset.cpus.effective": cpus, **limits})
    # For the host-ancestry preflight, stop at the namespace root;
    # init.scope is a sibling of the future service slice.
    if (info.st_dev, info.st_ino) == namespace_root:
        namespace_found = True
        break
assert namespace_found, ("guest cgroup namespace root is not on init PID ancestry", namespace_root)
after = (proc / "stat").read_text()
assert after[after.rfind(")") + 2:].split()[19] == start and (proc / "cgroup").read_text() == cgroup
assert (current.stat().st_dev, current.stat().st_ino) == namespace_root
print(json.dumps({"guest_host_pid": pid, "start_ticks": start, "cgroup": cgroup,
                  "guest_cgroup_root": {"device": namespace_root[0], "inode": namespace_root[1]},
                  "ancestors": rows}, sort_keys=True))
print("HOST_ANCESTOR_PREFLIGHT_PASS cpu=2 memory_min=8589934592 pids_min=256; service still uninstalled")
ANCESTORS
sudo docker exec "$guest" stat -c '%d %i' /sys/fs/cgroup >"$evidence/guest-cgroup-root-after.txt"
cmp "$evidence/guest-cgroup-root-before.txt" "$evidence/guest-cgroup-root-after.txt"
sudo docker cp "$payload" "$guest:/root/issue1162-install"
sudo docker exec "$guest" sh /root/issue1162-install/provision.sh | tee "$evidence/provision.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_live_probe.py --lease-self-test | tee "$evidence/guest-lease-probe-self-test.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_live_probe.py --self-test | tee "$evidence/guest-live-probe-self-test.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_stage_observer.py --self-test | tee "$evidence/guest-stage-observer-self-test.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_workspace_observer.py --self-test | tee "$evidence/guest-workspace-observer-self-test.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_manager_denial_probe.py --self-test | tee "$evidence/guest-manager-denial-self-test.txt"
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_broker_observer.py --self-test | tee "$evidence/guest-broker-observer-self-test.txt"
sudo docker exec "$guest" timeout --signal=TERM --kill-after=5 120 /root/issue1162-install/cleanup-identity-tests --cleanup-identity-only | tee "$evidence/guest-cleanup-identity-tests.txt"
sudo docker exec "$guest" systemctl start dbus.socket
sudo docker exec "$guest" install -d -m 0700 -o 0 -g 0 /root/issue1162-install/manager-denial-output
manager_denial_valid=false
set +e
sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_manager_denial_probe.py \
  --run-id "$GITHUB_RUN_ID" --run-attempt "${GITHUB_RUN_ATTEMPT:?}" \
  --output /root/issue1162-install/manager-denial-output \
  >"$evidence/manager-denial-console.log" 2>&1
manager_denial_status=$?
set -e
cat "$evidence/manager-denial-console.log"
if ! sudo docker cp "$guest:/root/issue1162-install/manager-denial-output" "$evidence/manager-denial-artifacts" || \
   ! sudo chown -R -- "$(id -u):$(id -g)" "$evidence/manager-denial-artifacts"; then
  echo "MANAGER_DENIAL_ARTIFACT_COPY_FAILED"
  manager_denial_status=1
fi
if (( manager_denial_status == 0 )); then manager_denial_valid=true; fi
# Keep collecting the independent service slice when a bare-account probe is
# inconclusive. Its missing evidence still prevents the overall validator PASS.
sudo docker exec "$guest" install -d -m 0700 -o 0 -g 0 /root/issue1162-install/broker-observer-output
timeout --signal=TERM --kill-after=5 4005 \
  sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_broker_observer.py --run \
    --run-id "$GITHUB_RUN_ID" --run-attempt "$GITHUB_RUN_ATTEMPT" --budget-seconds 4000 \
    --output /root/issue1162-install/broker-observer-output \
    >"$evidence/broker-observer-console.log" 2>&1 &
broker_observer_pid=$!
timeout --signal=TERM --kill-after=5 35 sudo docker exec -i "$guest" python3 - "$GITHUB_RUN_ID" "$GITHUB_RUN_ATTEMPT" <<'BROKER_READY' | tee "$evidence/broker-observer-ready.txt"
import json, os, pathlib, stat, sys, time
ready = pathlib.Path("/root/issue1162-install/broker-observer-output/ready.json")
deadline = time.monotonic() + 30
while not ready.exists() and time.monotonic() < deadline:
    time.sleep(0.05)
fd = os.open(ready, os.O_RDONLY | os.O_NOFOLLOW)
try:
    info = os.fstat(fd)
    assert stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and info.st_size <= 131072
    record = json.loads(os.read(fd, 131073))
finally:
    os.close(fd)
assert record["kind"] == "READY" and record["match_ack"] is True and record["subscribe_ack"] is True
assert str(record["run_id"]) == sys.argv[1] and record["run_attempt"] == int(sys.argv[2])
assert record["boot_id_raw"] == pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()
pid = record["observer_pid"]
assert type(pid) is int and pid > 1
raw = pathlib.Path(f"/proc/{pid}/stat").read_text()
assert int(raw[raw.rfind(")")+2:].split()[19]) == record["observer_start_ticks"]
print(json.dumps(record, sort_keys=True))
BROKER_READY
kill -0 "$broker_observer_pid"
entry_readback() {
  # Root readback outside every broker unit: exact tuples and bytes that each
  # gate PASS must repeat. Taken before activation and after the job finishes.
  sudo docker exec -i "$guest" python3 - <<'ENTRY_READBACK' >"$evidence/entry-readback-$1.json" || return 1
import hashlib, json, os
paths = {"gate": "/usr/local/libexec/buster-bench-broker-entry-gate",
         "broker": "/usr/local/libexec/buster-bench-systemd-broker",
         "receipt": "/etc/buster-bench/systemd-broker-accounts.identity",
         "passwd": "/etc/passwd", "group": "/etc/group", "nsswitch": "/etc/nsswitch.conf"}
files = {}
for name, path in paths.items():
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        before = os.fstat(fd)
        data = b""
        while chunk := os.read(fd, 1 << 20):
            data += chunk
        after = os.fstat(fd)
    finally:
        os.close(fd)
    assert (before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) == \
        (after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) and len(data) == after.st_size
    files[name] = {"path": path, "sha256": hashlib.sha256(data).hexdigest(), "uid": after.st_uid,
                   "gid": after.st_gid, "mode": after.st_mode, "nlink": after.st_nlink, "bytes": len(data),
                   "tuple": "%d:%d:%d:%d:%d:%d:%d" % (after.st_dev, after.st_ino, after.st_size,
                            after.st_mtime_ns // 10**9, after.st_mtime_ns % 10**9,
                            after.st_ctime_ns // 10**9, after.st_ctime_ns % 10**9)}
print(json.dumps({"schema": "issue1162-broker-entry-readback-v1", "files": files}, sort_keys=True))
ENTRY_READBACK
  cat "$evidence/entry-readback-$1.json"
}
entry_readback before
sudo docker exec "$guest" cat /etc/nsswitch.conf | tee "$evidence/guest-nsswitch.conf"
sudo docker exec "$guest" systemctl start buster-bench-systemd-broker.socket
sudo docker exec "$guest" systemctl start buster-bench.service
sudo docker exec "$guest" systemctl show buster-bench.service -p ActiveState -p MainPID -p ControlGroup -p RestrictSUIDSGID -p NoNewPrivileges -p CapabilityBoundingSet | tee "$evidence/service-effective.txt"
sudo docker exec "$guest" systemctl is-active --quiet buster-bench.service
key="issue1162-${GITHUB_RUN_ID}"
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway capabilities | tee "$evidence/gateway-capabilities.txt"
if [[ "${BQ_P_REPRO:-}" == 1 ]]; then
  # Scratch #880 attempt-P reproduction only: trace the service and its manager helpers.
  sudo docker exec "$guest" install -d -m 0700 /root/p-strace
  svcpid="$(sudo docker exec "$guest" systemctl show -p MainPID --value buster-bench.service)"
  sudo docker exec -d "$guest" strace -f -tt -T -s 256 -o /root/p-strace/run -p "$svcpid"
  sleep 2
fi
sudo docker exec "$guest" sh -ec 'test -f /var/lib/buster-bench/lease/host.lock; test ! -L /var/lib/buster-bench/lease/host.lock; stat -c "%d %i %h %a %u %g" /var/lib/buster-bench/lease/host.lock' >"$evidence/lease-before-submit.txt"
read -r lease_device lease_inode lease_links lease_mode lease_uid lease_gid lease_extra <"$evidence/lease-before-submit.txt"
[[ "$lease_device" =~ ^[0-9]+$ && "$lease_inode" =~ ^[1-9][0-9]*$ &&
   "$lease_links" == 1 && "$lease_mode" == 640 && "$lease_uid" == 65000 &&
   "$lease_gid" == 65000 && -z "$lease_extra" ]]
wait_limit_seconds=3900
wait_started=$SECONDS
wait_deadline=$((wait_started + wait_limit_seconds))
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway submit "$key" "$baseline" "$subject" | tee "$evidence/submit.txt"
job="$(sed -nE 's/^job=([0-9]+) .*/\1/p' "$evidence/submit.txt" | head -1)"
request_sha="$(sed -nE 's/.*request-sha256=([a-f0-9]{64}).*/\1/p' "$evidence/submit.txt" | head -1)"
test -n "$job"
test -n "$request_sha"
echo "JOB=$job"
if [[ "${BQ_P_REPRO:-}" == 1 ]]; then
  # Scratch #880 attempt-P reproduction: one SIGTERM to the service MainPID at the
  # first moment the prepare manifest exists, then capture, controlled restart and
  # a traced recovery window. Never a qualification result.
  sudo docker exec -i "$guest" python3 - "$job" <<'PWATCH' | tee "$evidence/p-watch.log"
import glob, os, signal, subprocess, sys, time
job = sys.argv[1]
root = "/var/lib/buster-bench/workspaces/results"
main = int(subprocess.check_output(["systemctl", "show", "-p", "MainPID", "--value", "buster-bench.service"]).strip())
deadline = time.monotonic() + 600
while time.monotonic() < deadline:
    hits = glob.glob(f"{root}/job-{job}-attempt-*/validate-buster-v1.prepare.manifest")
    if hits:
        directory = os.path.dirname(hits[0])
        os.kill(main, signal.SIGTERM)
        print(f"{time.time_ns()} SIGTERM pid={main} dir={directory} present={sorted(os.listdir(directory))}", flush=True)
        break
    time.sleep(0.001)
else:
    print("EXPIRED", flush=True)
    sys.exit(1)
PWATCH
  for _ in $(seq 1 600); do
    state="$(sudo docker exec "$guest" systemctl is-active buster-bench.service || true)"
    if [[ "$state" != active && "$state" != deactivating ]]; then break; fi
    sleep 0.1
  done
  sleep 3
  p_capture() {
    local tag=$1
    sudo docker exec "$guest" systemctl list-units --all --no-pager 'buster-bench*' >"$evidence/p-units-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'for u in $(systemctl list-units --all --plain --no-legend "buster-bench*" | awk "{print \$1}"); do systemctl show "$u" -p Id -p LoadState -p ActiveState -p SubState -p Result -p MainPID -p InvocationID -p ControlGroup -p ExecMainStatus -p CollectMode; echo; done' >"$evidence/p-unit-show-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" journalctl --no-pager -b -o short-precise >"$evidence/p-journal-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'find /var/lib/buster-bench -xdev -printf "%m %u:%g %s %T@ %p\n" | sort' >"$evidence/p-state-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'cd /var/lib/buster-bench/queue && for f in *; do [ "$f" = journal ] && continue; echo "== $f"; head -c 2048 "$f"; echo; done; ls -la; od -A d -t x1 journal | tail -n 60' >"$evidence/p-queue-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'for f in /var/lib/buster-bench/workspaces/results/job-*/validate-buster-v1.*; do echo "== $f"; head -c 4096 "$f"; echo; done' >"$evidence/p-result-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" cat /proc/locks >"$evidence/p-locks-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" systemctl status --no-pager buster-bench.service >"$evidence/p-service-status-$tag.txt" 2>&1 || true
  }
  p_capture after-sigterm
  sudo docker exec "$guest" pkill -INT -x strace || true
  sleep 1
  if [[ -n "${BQ_P_REPRO_UPGRADE:-}" ]]; then
    # Replace only the service binary with the fix build while the job is stuck,
    # then let the controlled restart recover it (the planned job-24 disposition).
    git worktree add --detach "$RUNNER_TEMP/p-upgrade" "$BQ_P_REPRO_UPGRADE"
    (cd "$RUNNER_TEMP/p-upgrade" && mkdir -p build &&
     clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o build/buster-bench-build &&
     build/buster-bench-build bench_service capabilities >/dev/null)
    git -C "$RUNNER_TEMP/p-upgrade" rev-parse HEAD | tee "$evidence/p-upgrade-commit.txt"
    sha256sum "$RUNNER_TEMP/p-upgrade/build/bench-service-tools/service" | tee "$evidence/p-upgrade-service-sha256.txt"
    sudo docker cp "$RUNNER_TEMP/p-upgrade/build/bench-service-tools/service" "$guest:/root/p-upgrade-service"
    sudo docker exec "$guest" install -o root -g root -m 0755 /root/p-upgrade-service /usr/local/libexec/buster-bench-service
    sudo docker exec "$guest" sha256sum /usr/local/libexec/buster-bench-service | tee -a "$evidence/p-upgrade-service-sha256.txt"
  fi
  sudo docker exec "$guest" systemctl start buster-bench.service || true
  newpid="$(sudo docker exec "$guest" systemctl show -p MainPID --value buster-bench.service)"
  echo "P_REPRO restarted MainPID=$newpid"
  sudo docker exec "$guest" timeout 8 strace -f -tt -T -s 256 -o /root/p-strace/recovery -p "$newpid" || true
  for _ in $(seq 1 30); do
    sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway result "$job" >"$evidence/p-result-after-restart.txt" 2>&1 || true
    if grep -q 'phase=finished' "$evidence/p-result-after-restart.txt"; then break; fi
    sleep 1
  done
  cat "$evidence/p-result-after-restart.txt"
  sleep 2
  p_capture after-restart
  sudo docker exec "$guest" tar -C /root -czf - p-strace >"$evidence/p-strace.tgz" 2>"$evidence/p-strace-tar.log" || true
  echo "BQ_P_REPRO_DONE job=$job"
  exit 0
fi
observer_valid=false
observer_budget=$((wait_deadline - SECONDS - 35))
if (( observer_budget > 0 )); then
  # Observe concurrently before waiting for the separate active base-build
  # probe. The timeout owns only this observer client, never a service unit.
  timeout --signal=TERM --kill-after=5 "$observer_budget" \
    sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_stage_observer.py --run \
      --job "$job" --request-sha256 "$request_sha" --baseline "$baseline" --subject "$subject" \
      --budget-seconds "$observer_budget" --output /root/issue1162-install/stage-observer-output \
      >"$evidence/stage-observer-console.log" 2>&1 &
  observer_pid=$!
else
  echo "STAGE_OBSERVER_INCONCLUSIVE insufficient submit-relative deadline budget" | tee "$evidence/stage-observer-console.log"
fi
probe_valid=false
probe_budget=$((wait_deadline - SECONDS - 35))
if (( probe_budget > 0 )); then
  set +e
  sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_live_probe.py --run \
    --job "$job" --request-sha256 "$request_sha" --baseline "$baseline" --subject "$subject" \
    --budget-seconds "$probe_budget" --output /root/issue1162-install/live-probe-output \
    >"$evidence/live-probe-console.log" 2>&1
  probe_status=$?
  set -e
  cat "$evidence/live-probe-console.log"
  if ! sudo docker cp "$guest:/root/issue1162-install/live-probe-output" "$evidence/live-probe-artifacts" || \
     ! sudo chown -R -- "$(id -u):$(id -g)" "$evidence/live-probe-artifacts"; then
    echo "LIVE_PROBE_ARTIFACT_COPY_FAILED"
    probe_status=1
  fi
  if (( probe_status == 0 )); then probe_valid=true; fi
else
  echo "LIVE_PROBE_INCONCLUSIVE insufficient submit-relative deadline budget" | tee "$evidence/live-probe-console.log"
fi
if [[ "$probe_valid" != true ]]; then
  echo "LIVE_PROBE_VALIDATION=INCONCLUSIVE; continue bounded RESULT wait to retain actual service outcome"
fi
finished=false
# Client-side ceiling only: 60m maximum job plus 5m result/transport slack.
# It does not change the service runtime policy or the 90m outer CI limit.
# The client call can wait 30s; stop starting calls with less than 35s remaining.
while (( SECONDS + 35 < wait_deadline )); do
  set +e
  result="$(sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway result "$job" 2>&1)"
  status=$?
  set -e
  printf 'elapsed_seconds=%s exit_status=%s\n%s\n' "$((SECONDS - wait_started))" "$status" "$result" >>"$evidence/result-wait.log"
  if [[ $status -eq 0 ]]; then
    if [[ "$result" != "job=$job "* ]]; then
      printf 'unexpected gateway result receipt for job %s: %s\n' "$job" "$result" >&2
      exit 1
    fi
    printf '%s\n' "$result" >"$evidence/status-latest.txt"
    if [[ "$result" == *"phase=finished"* ]]; then
      printf '%s\n' "$result" | tee "$evidence/result.txt"
      finished=true
      break
    fi
  elif [[ "$result" == "bench_service: io-uncertain" || "$result" == "bench_service: busy" ]]; then
    :
  else
    printf '%s\n' "$result" >&2
    exit "$status"
  fi
  sleep 10
done
if [[ "$finished" != true ]]; then
  printf 'RESULT_WAIT_TIMEOUT limit=%ss elapsed=%ss\n' "$wait_limit_seconds" "$((SECONDS - wait_started))" | tee "$evidence/result-wait-timeout.txt"
  exit 1
fi
python3 "$live_probe_helper" --check-result "$evidence/result.txt" \
  --job "$job" --request-sha256 "$request_sha" >"$evidence/result-validated.txt"
read -r token digest result_extra <"$evidence/result-validated.txt"
[[ "$token" =~ ^[1-9][0-9]*$ && "$digest" =~ ^[a-f0-9]{64}$ && -z "$result_extra" ]]
terminal_valid=false
terminal_budget=$((wait_deadline - SECONDS - 35))
if (( terminal_budget > 60 )); then terminal_budget=60; fi
if (( terminal_budget > 0 )); then
  set +e
  sudo docker exec "$guest" python3 /root/issue1162-install/issue1162_live_probe.py --terminal \
    --job "$job" --attempt "$token" --request-sha256 "$request_sha" \
    --baseline "$baseline" --subject "$subject" --lease-device "$lease_device" --lease-inode "$lease_inode" \
    --budget-seconds "$terminal_budget" --output /root/issue1162-install/terminal-proof-output \
    >"$evidence/terminal-proof-console.log" 2>&1
  terminal_status=$?
  set -e
  cat "$evidence/terminal-proof-console.log"
  if ! sudo docker cp "$guest:/root/issue1162-install/terminal-proof-output" "$evidence/terminal-proof-artifacts" || \
     ! sudo chown -R -- "$(id -u):$(id -g)" "$evidence/terminal-proof-artifacts"; then
    echo "TERMINAL_PROOF_ARTIFACT_COPY_FAILED"
    terminal_status=1
  fi
  if (( terminal_status == 0 )); then terminal_valid=true; fi
else
  echo "TERMINAL_PROOF_INCONCLUSIVE insufficient submit-relative deadline budget" | tee "$evidence/terminal-proof-console.log"
fi
if [[ "$terminal_valid" != true ]]; then
  echo "TERMINAL_PROOF_VALIDATION=INCONCLUSIVE; retain authenticated export and replay of actual service outcome"
fi
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway export "$job" "$token" "$digest" >"$evidence/result.bqexport" 2>"$evidence/export-stderr.txt"
cat "$evidence/export-stderr.txt"
receipt="$(sed -nE 's/^export-receipt-sha256=([a-f0-9]{64})$/\1/p' "$evidence/export-stderr.txt" | head -1)"
test -n "$receipt"
sha256sum "$evidence/result.bqexport" | tee "$evidence/archive-sha256.txt"
mkdir -m 0700 "$evidence/replay"
build/bench-service-tools/service unpack-export "$evidence/result.bqexport" "$evidence/replay/result" "$receipt" | tee "$evidence/replay.txt"
if [[ -n "$observer_pid" ]]; then
  set +e
  wait "$observer_pid"
  observer_status=$?
  set -e
  printf 'exit_status=%s\n' "$observer_status" >"$evidence/stage-observer-exit.txt"
  cat "$evidence/stage-observer-console.log"
  if ! collect_stage_observer; then observer_status=1; fi
  if (( observer_status == 0 )); then observer_valid=true; fi
fi
frozen_tree_valid=false
if python3 "$frozen_tree_helper" verify \
  --observer-dir "$evidence/stage-observer-artifacts" \
  --base-receipt "$evidence/replay/result/validate-buster-v1.base-build.inventory" \
  --candidate-receipt "$evidence/replay/result/validate-buster-v1.candidate-build.inventory" \
  --source-commit "$subject" --source-tree "$subject_tree" --build-blob "$subject_build_blob" \
  --output "$evidence/frozen-tree-reconciliation.json" \
  >"$evidence/frozen-tree-reconciliation.log" 2>&1; then
  frozen_tree_valid=true
fi
cat "$evidence/frozen-tree-reconciliation.log"
# The legacy observer verdict and timestamps remain unchanged. The new gate
# requires the source's reviewed durable-before-next-launch ordering plus
# complete independent equality; a late external scan is still reported late.
broker_valid=false
entry_readback_after=false
if entry_readback after; then entry_readback_after=true; fi
if [[ "$entry_readback_after" == true ]] && collect_broker_observer && capture_broker_journal; then
  python3 - "$evidence" "$payload" "$GITHUB_RUN_ID" "$GITHUB_RUN_ATTEMPT" "$job" "$token" "$subject" "$subject_tree" <<'BROKER_INPUTS'
import hashlib, json, pathlib, sys
root, payload = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
run_id, run_attempt, job, attempt = sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
ready = json.loads((root / "broker-observer-artifacts/ready.json").read_text())
assert ready["run_id"] == run_id and ready["run_attempt"] == run_attempt
capture = json.loads((root / "broker-journal-capture.json").read_text())
requests = [{"operation": 1, "stage": stage, "job": job, "attempt": attempt} for stage in range(6)]
requests += [{"operation": 2, "stage": 0, "job": job, "attempt": value}
             for value in (attempt, 999998 if attempt == 999999 else 999999)]
expected = {"schema": "issue1162-broker-evidence-expectation-v1", "run_id": run_id,
    "run_attempt": run_attempt, "job_id": job, "job_attempt": attempt,
    "boot_id": ready["boot_id"], "boot_id_raw": ready["boot_id_raw"],
    "source_commit": sys.argv[7], "source_tree": sys.argv[8],
    "broker_binary_sha256": hashlib.sha256((payload / "binaries/buster-bench-systemd-broker").read_bytes()).hexdigest(),
    "broker_executable_path": "/usr/local/libexec/buster-bench-systemd-broker",
    "profile": {"broker_uid": 0, "broker_gid": 65000, "candidate_gid": 65001,
        "client_uids": [65000], "groups": [65000, 65001],
        "capabilities": {key: "0000000000000000" for key in ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb")},
        "no_new_privs": 1, "seccomp": 2, "seccomp_filters_min": 1,
        "read_only_mount_targets": ["/", "/etc/buster-bench", "/opt/buster-bench/installed", "/var/lib/buster-bench"],
        "cgroup_prefix": "/system.slice/system-buster\\x2dbench\\x2dsystemd\\x2dbroker.slice/"},
    "allowed_requests": requests,
    "journal_capture": {key: capture[key] for key in ("complete", "sha256", "bytes", "records")}}
(root / "broker-expected.json").write_text(json.dumps(expected, sort_keys=True) + "\n")
def reference(path):
    raw = path.read_bytes()
    return {"path": str(path.relative_to(root)), "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}
overlap = []
for index in range(8):
    path = root / f"live-probe-artifacts/broker-{index}.capture.json"
    if not path.exists():
        continue
    value = json.loads(path.read_text())
    process = value.get("process")
    if not isinstance(process, dict):
        continue
    properties = value["properties"]
    row = {"schema": "issue1162-broker-overlap-v1", "boot_id": ready["boot_id"],
        "unit": properties["Id"], "invocation_id": properties["InvocationID"],
        "pid": process["pid"], "start_ticks": process["starttime_ticks"], "capture_json": reference(path)}
    for field, suffix in (("status", "proc-status"), ("mountinfo", "proc-mountinfo"), ("cgroup", "proc-cgroup")):
        row[field] = reference(root / f"live-probe-artifacts/broker-{index}.{suffix}")
    overlap.append(row)
(root / "broker-overlap.jsonl").write_text("".join(json.dumps(row, sort_keys=True) + "\n" for row in overlap))
BROKER_INPUTS
  # Retained, not required: the earlier every-broker external /proc consumer.
  # Its components are reported; the millisecond race it lost in Attempt 23
  # is replaced by the #1162 same-PID entry criterion below.
  set +e
  python3 "$broker_evidence_helper" --journal-jsonl "$evidence/broker-journal.jsonl" \
    --observer-dir "$evidence/broker-observer-artifacts" --expected-json "$evidence/broker-expected.json" \
    --overlap-jsonl "$evidence/broker-overlap.jsonl" >"$evidence/broker-reconciliation.json" \
    2>"$evidence/broker-reconciliation-stderr.txt"
  printf 'external_consumer_exit=%s\n' "$?" | tee "$evidence/broker-reconciliation-exit.txt"
  set -e
  python3 - "$evidence" "$payload" <<'ENTRY_INPUTS'
import hashlib, json, pathlib, sys
root, payload = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
digest = lambda name: hashlib.sha256((payload / "binaries" / name).read_bytes()).hexdigest()
(root / "broker-entry-expected.json").write_text(json.dumps({
    "schema": "issue1162-broker-entry-expectation-v1",
    "gate_sha256": digest("buster-bench-broker-entry-gate"),
    "broker_sha256": digest("buster-bench-systemd-broker"),
    "accounts": [65000, 65000, 65001, 65001, 65002, 65002],
    "readbacks": [str(root / "entry-readback-before.json"), str(root / "entry-readback-after.json")]},
    sort_keys=True) + "\n")
ENTRY_INPUTS
  # The observer exits 1 when a racing /proc capture is incomplete; that is
  # retained, not required. A timeout-killed observer (124) is not complete.
  if [[ "$broker_observer_status" =~ ^[01]$ ]] && python3 "$broker_entry_helper" --journal-jsonl "$evidence/broker-journal.jsonl" \
    --observer-dir "$evidence/broker-observer-artifacts" --expected-json "$evidence/broker-expected.json" \
    --entry-expected-json "$evidence/broker-entry-expected.json" \
    --overlap-jsonl "$evidence/broker-overlap.jsonl" >"$evidence/broker-entry-reconciliation.json" \
    2>"$evidence/broker-entry-reconciliation-stderr.txt"; then
    broker_valid=true
  fi
  cat "$evidence/broker-entry-reconciliation.json"
  cat "$evidence/broker-reconciliation.json"
fi
if [[ "$probe_valid" != true || "$terminal_valid" != true || "$frozen_tree_valid" != true || "$broker_valid" != true || "$manager_denial_valid" != true ]]; then
  echo "SERVICE_RESULT_SUCCEEDED source=$subject job=$job token=$token; live_probe_valid=$probe_valid terminal_proof_valid=$terminal_valid legacy_stage_observer_valid=$observer_valid frozen_tree_reconciled=$frozen_tree_valid broker_reconciled=$broker_valid manager_denial_valid=$manager_denial_valid; full acceptance pending"
  exit 1
fi
echo "NORMAL_PATH_EXECUTION_PASS source=$subject job=$job token=$token; live, terminal, independent frozen-tree reconciliation, per-activation manager/observer/gate/broker/outcome reconciliation and bare-account manager probes passed; legacy_stage_observer_valid=$observer_valid remains separately reported; full acceptance pending"
