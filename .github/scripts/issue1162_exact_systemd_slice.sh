#!/usr/bin/env bash
# Temporary, isolated #1162 validation transport. Never install on a protected host.
set -Eeuo pipefail
subject=12e6dfe83680bd636e4ed32c2c08a66cf4b4ab57
subject_tree=42fdf0b4f8c960d5d2fdc8d222ef62f36e3d58ab
subject_build_blob=1f9b3ab8350dbe4c8a2f24e288a72ecf7b949359
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
    "12e6dfe83680bd636e4ed32c2c08a66cf4b4ab57": (379, 43099, "d1d4864fe649f63c3f0552558fc261e65ede89aa3d84e1898b7a12d4fafdfa89"),
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
    BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = BQ_BROKER_VERSION,
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
if [[ "${BQ_L_REPRO:-}" == 1 ]]; then
  # Scratch #880 attempt-L reproduction: a buster-bench peer that connects to the
  # first job's .lease-handoff, sends nothing, never reads, holds 6.5 s.
  sudo docker exec -i "$guest" tee /root/l-peer.py >/dev/null <<'LPEER'
import os, socket, sys, time
path = "/var/lib/buster-bench/workspaces/results/job-1-attempt-2/.lease-handoff"
log = open("/tmp/l-peer.log", "a", buffering=1)
def emit(m): log.write("%d %s\n" % (time.time_ns(), m)); log.flush()
emit("ARMED uid=%d gid=%d" % (os.geteuid(), os.getegid()))
deadline = time.time() + 900
while True:
    try:
        st = os.lstat(path); break
    except FileNotFoundError:
        pass
    if time.time() > deadline:
        emit("EXPIRED"); sys.exit(3)
    time.sleep(0.001)
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.connect(path)
emit("CONNECTED ctime_ns=%d" % st.st_ctime_ns)
time.sleep(6.5)
s.close()
emit("CLOSED")
LPEER
  sudo docker exec "$guest" chmod 0444 /root/l-peer.py
  sudo docker exec "$guest" install -m 0644 /root/l-peer.py /tmp/l-peer.py
  sudo docker exec -d "$guest" runuser -u buster-bench -g buster-bench -- python3 /tmp/l-peer.py
  sleep 1
fi
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
if [[ "${BQ_L_REPRO:-}" == 1 ]]; then
  BQ=/usr/local/libexec/buster-bench-service
  lcap() {
    local tag=$1
    sudo docker exec "$guest" sh -c 'cat /tmp/l-peer.log' >"$evidence/l-peer-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" systemctl list-units --all --no-pager 'buster-bench*' >"$evidence/l-units-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" journalctl --no-pager -b -o short-precise >"$evidence/l-journal-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'cd /var/lib/buster-bench/queue && ls -la && for f in *; do [ "$f" = journal ] && continue; echo "== $f"; head -c 2048 "$f"; echo; done; od -A d -t x1 journal | tail -n 30' >"$evidence/l-queue-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'find /var/lib/buster-bench/workspaces -maxdepth 3 -printf "%m %u:%g %s %p\n" | sort; for f in /var/lib/buster-bench/workspaces/results/job-1-attempt-2/validate-buster-v1.*; do echo "== $f"; head -c 2048 "$f"; echo; done' >"$evidence/l-state-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" cat /proc/locks >"$evidence/l-locks-$tag.txt" 2>&1 || true
  }
  sleep 25
  sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway result "$job" 2>&1 | tee "$evidence/l-gateway-after.txt" || true
  lcap after
  cat "$evidence/l-peer-after.txt"
  grep -E "handoff|lease|buster-bench-1-2|broker@" "$evidence/l-journal-after.txt" | tail -n 60 || true
  if grep -q 'phase=finished' "$evidence/l-gateway-after.txt"; then
    echo "L_REPRO outcome=a"
  else
    echo "L_REPRO outcome=b; stopping the service for the reconcile"
    sudo docker exec "$guest" systemctl stop buster-bench.service buster-bench-systemd-broker.socket || true
    sudo docker exec "$guest" systemctl show buster-bench.service -p Result -p ExecMainStatus -p MainPID || true
    sudo docker exec "$guest" runuser -u buster-bench -g buster-bench -- $BQ result /var/lib/buster-bench/queue "$job" 2>&1 | tee "$evidence/l-local-result-before.txt" || true
    lcap before-reconcile
    set +e
    sudo docker exec "$guest" runuser -u buster-bench -g buster-bench -- flock -n -E 75 /var/lib/buster-bench/lease/host.lock $BQ workspace-reconcile /var/lib/buster-bench/queue /var/lib/buster-bench/workspaces "$job" 2 >"$evidence/l-reconcile.txt" 2>&1
    echo "L_REPRO reconcile_exit=$?" | tee -a "$evidence/l-reconcile.txt"
    set -e
    cat "$evidence/l-reconcile.txt"
    sudo docker exec "$guest" runuser -u buster-bench -g buster-bench -- $BQ result /var/lib/buster-bench/queue "$job" 2>&1 | tee "$evidence/l-local-result-after.txt" || true
    lcap after-reconcile
    sudo docker exec "$guest" systemctl start buster-bench-systemd-broker.socket buster-bench.service || true
    sleep 3
    sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway result "$job" 2>&1 | tee "$evidence/l-gateway-restart.txt" || true
    lcap after-restart
  fi
  full="$(sed -nE 's/^full-result-sha256=([a-f0-9]{64})$/\1/p' "$evidence/l-gateway-restart.txt" "$evidence/l-gateway-after.txt" 2>/dev/null | head -1)"
  if [[ -n "$full" ]]; then
    sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway export "$job" 2 "$full" >"$evidence/l-export.bin" 2>"$evidence/l-export-receipt.txt" || echo "L_REPRO export failed"
    sha256sum "$evidence/l-export.bin"; cat "$evidence/l-export-receipt.txt"
  fi
  echo "BQ_L_REPRO_DONE job=$job"
  exit 0
fi
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
  if [[ -n "${BQ_P_REPRO_LOOP_SECONDS:-}" ]]; then
    # Mirror the host: the old binary is restarted and spins in recovery, then
    # the operator stops it before the binary is replaced.
    sudo docker exec "$guest" systemctl start buster-bench.service || true
    looppid="$(sudo docker exec "$guest" systemctl show -p MainPID --value buster-bench.service)"
    echo "P_REPRO old-binary restart MainPID=$looppid"
    sleep "$BQ_P_REPRO_LOOP_SECONDS"
    sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway result "$job" >"$evidence/p-gateway-in-loop.txt" 2>&1 || true
    cat "$evidence/p-gateway-in-loop.txt"
    p_capture in-loop
    stop_started=$(date +%s.%N)
    sudo docker exec "$guest" systemctl stop buster-bench.service || true
    stop_ended=$(date +%s.%N)
    echo "P_REPRO stop started=$stop_started ended=$stop_ended" | tee "$evidence/p-stop-timing.txt"
    sudo docker exec "$guest" systemctl show buster-bench.service -p ActiveState -p SubState -p Result -p ExecMainStatus -p ExecMainCode -p MainPID | tee -a "$evidence/p-stop-timing.txt"
    p_capture after-stop
  fi
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
  cp "$evidence/p-result-after-restart.txt" "$evidence/p-gateway-after-restart.txt"
  sleep 2
  p_capture after-restart
  if [[ -n "${BQ_P_REPRO_LOOP_SECONDS:-}" ]] && grep -q 'phase=finished' "$evidence/p-gateway-after-restart.txt"; then
    # Export the finalized cancelled attempt and replay it, as the host would.
    token="$(sed -nE 's/^job=[0-9]+ token=([0-9]+) .*/\1/p' "$evidence/p-gateway-after-restart.txt" | head -1)"
    full="$(sed -nE 's/^full-result-sha256=([a-f0-9]{64})$/\1/p' "$evidence/p-gateway-after-restart.txt" | head -1)"
    sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway export "$job" "$token" "$full" >"$evidence/p-export.bin" 2>"$evidence/p-export-receipt.txt" || echo "P_REPRO export exit=$?"
    sha256sum "$evidence/p-export.bin" | tee "$evidence/p-export-sha256.txt"
    cat "$evidence/p-export-receipt.txt"
    receipt="$(sed -nE 's/^export-receipt-sha256=([a-f0-9]{64})$/\1/p' "$evidence/p-export-receipt.txt")"
    sudo docker cp "$evidence/p-export.bin" "$guest:/root/p-export.bin"
    sudo docker exec "$guest" sh -c "install -d -m 0700 /root/p-unpack-parent && /usr/local/libexec/buster-bench-service unpack-export /root/p-export.bin /root/p-unpack-parent/out $receipt; echo unpack-exit=\$?; find /root/p-unpack-parent/out -type f | sort | xargs sha256sum" | tee "$evidence/p-unpack.txt" || true
  fi
  sudo docker exec "$guest" tar -C /root -czf - p-strace >"$evidence/p-strace.tgz" 2>"$evidence/p-strace-tar.log" || true
  echo "BQ_P_REPRO_DONE job=$job"
  exit 0
fi
if [[ "${BQ_K_REPRO:-}" == 1 ]]; then
  # Scratch #881 lane C (#1878) keeper and isolation checks on the exact subject:
  #  (a) the lease keeper lives inside the real outer unit and stops with it;
  #  (b) a stage cannot reach results/.lease-return (mount + connect), and a
  #      buster-bench peer inside buster-bench.slice is refused by the keeper;
  #  (c) a restarted service reclaims the lease from the keeper before TERM,
  #      including the production /proc/<pid>/cgroup peer check;
  #  (d) broker v2 (184-byte) frames for start, TERM and KILL on smoke units.
  # Never a qualification result; retained diagnostics only.
  sudo docker exec "$guest" install -d -m 0700 /root/k-out /root/k-strace
  sudo docker exec -i "$guest" tee /root/k-watch.py >/dev/null <<'KWATCH'
import errno, json, os, re, stat, subprocess, sys, time
job = int(sys.argv[1]); budget = float(sys.argv[2])
SLICE = "/sys/fs/cgroup/buster.slice/buster-bench.slice"
RET = "/var/lib/buster-bench/workspaces/results/.lease-return"
LEASE = "/var/lib/buster-bench/lease/host.lock"
STAGES = ("base-generate", "base-build", "candidate-generate", "candidate-build", "throughput")
deadline = time.monotonic() + budget
def emit(line):
    print("%d %s" % (time.time_ns(), line), flush=True)
def procs(cg):
    try:
        return [int(x) for x in open(cg + "/cgroup.procs").read().split()]
    except OSError:
        return []
def status(pid):
    return dict(l.split(":\t", 1) for l in open(f"/proc/{pid}/status").read().splitlines() if ":\t" in l)
def show(unit, *props):
    args = ["systemctl", "show", unit] + ["-p" + p for p in props]
    return dict(l.split("=", 1) for l in subprocess.run(args, capture_output=True, text=True).stdout.splitlines() if "=" in l)
pat = re.compile(r"buster-bench-%d-([0-9]+)\.service$" % job)
attempt = None
while attempt is None and time.monotonic() < deadline:
    try:
        for name in os.listdir(SLICE):
            m = pat.match(name)
            if m:
                attempt = int(m.group(1))
    except FileNotFoundError:
        pass
    if attempt is None:
        time.sleep(0.01)
if attempt is None:
    emit("K_A_FAIL outer unit never appeared"); sys.exit(2)
outer = f"buster-bench-{job}-{attempt}.service"
cg = f"{SLICE}/{outer}"
sock = f"{RET}/{job}-{attempt}"
print(f"K_VAR attempt={attempt}", flush=True)
while time.monotonic() < deadline:
    try:
        if stat.S_ISSOCK(os.lstat(sock).st_mode):
            break
    except FileNotFoundError:
        pass
    time.sleep(0.005)
else:
    emit(f"K_A_FAIL keeper socket {sock} never appeared"); sys.exit(2)
lease_st = os.stat(LEASE)
def unix_inodes(pid):
    found = set()
    try:
        for line in open(f"/proc/{pid}/net/unix").read().splitlines()[1:]:
            f = line.split()
            if len(f) >= 8 and f[7] == sock and int(f[3], 16) & 0x10000:
                found.add(int(f[6]))
    except OSError:
        pass
    return found
keeper, rows = None, []
for _ in range(500):
    rows = []
    for pid in procs(cg):
        try:
            fds = {}
            for fd in os.listdir(f"/proc/{pid}/fd"):
                try:
                    fds[fd] = os.readlink(f"/proc/{pid}/fd/{fd}")
                except OSError:
                    pass
            inodes = unix_inodes(pid)
            lease_fds = []
            for fd in fds:
                try:
                    s = os.stat(f"/proc/{pid}/fd/{fd}")
                    if (s.st_dev, s.st_ino) == (lease_st.st_dev, lease_st.st_ino):
                        lease_fds.append(int(fd))
                except OSError:
                    pass
            st = status(pid)
            row = {"pid": pid, "ppid": int(st["PPid"]), "uid": st["Uid"].split()[0], "gid": st["Gid"].split()[0],
                   "exe": os.readlink(f"/proc/{pid}/exe"),
                   "cmdline": open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ").decode(errors="replace").strip(),
                   "cgroup": open(f"/proc/{pid}/cgroup").read().strip(),
                   "listening_keeper_socket": any(v == f"socket:[{i}]" for v in fds.values() for i in inodes),
                   "lease_fds": sorted(lease_fds), "no_new_privs": st.get("NoNewPrivs"), "seccomp": st.get("Seccomp")}
            rows.append(row)
            if row["listening_keeper_socket"]:
                keeper = row
        except (OSError, KeyError, ValueError):
            continue
    if keeper:
        break
    time.sleep(0.01)
props = show(outer, "MainPID", "ControlGroup", "InvocationID", "ActiveState", "RuntimeMaxUSec", "InaccessiblePaths")
sock_st = os.lstat(sock)
dir_st = os.lstat(RET)
dev = "%02x:%02x:%d" % (os.major(lease_st.st_dev), os.minor(lease_st.st_dev), lease_st.st_ino)
locks = [l for l in open("/proc/locks").read().splitlines() if dev in l]
record = {"outer": outer, "properties": props, "processes": rows, "keeper": keeper, "socket": sock,
          "socket_mode": oct(stat.S_IMODE(sock_st.st_mode)), "socket_uid": sock_st.st_uid,
          "directory_mode": oct(stat.S_IMODE(dir_st.st_mode)), "directory_uid": dir_st.st_uid,
          "lease_locks": locks}
json.dump(record, open("/root/k-out/a-keeper.json", "w"), indent=2, sort_keys=True)
if keeper:
    print(f"K_VAR keeper={keeper['pid']}", flush=True)
    if keeper["lease_fds"]:
        print(f"K_VAR keeper_fd={keeper['lease_fds'][0]}", flush=True)
    emit(f"K_A_KEEPER_IN_OUTER unit={outer} keeper_pid={keeper['pid']} keeper_ppid={keeper['ppid']} "
         f"outer_main_pid={props.get('MainPID')} keeper_exe={keeper['exe']} keeper_cgroup={keeper['cgroup']} "
         f"outer_cgroup={props.get('ControlGroup')} keeper_lease_fds={keeper['lease_fds']} "
         f"uid={keeper['uid']} gid={keeper['gid']} socket_mode={record['socket_mode']} "
         f"dir_mode={record['directory_mode']} lease_locks={len(locks)} outer_processes={len(rows)}")
else:
    emit(f"K_A_FAIL no process in {cg} listens on {sock}; processes={json.dumps(rows)}")
# (b1) every stage that becomes live: its mount namespace, as its own uid and
# as the service uid, must not reach the keeper directory or connect.
probe = r'''
import errno, os, socket, stat, sys
d, s = sys.argv[1], sys.argv[2]
r = {"uid": os.getuid(), "gid": os.getgid()}
try:
    st = os.stat(d); r["dir"] = "mode=%o,uid=%d" % (stat.S_IMODE(st.st_mode), st.st_uid)
except OSError as e:
    r["dir"] = errno.errorcode[e.errno]
try:
    r["list"] = "ok:%d" % len(os.listdir(d))
except OSError as e:
    r["list"] = errno.errorcode[e.errno]
try:
    os.lstat(s); r["socket"] = "visible"
except OSError as e:
    r["socket"] = errno.errorcode[e.errno]
c = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
try:
    c.connect(s); r["connect"] = "CONNECTED"
except OSError as e:
    r["connect"] = errno.errorcode[e.errno]
c.close()
print(" ".join("%s=%s" % kv for kv in r.items()))
'''
probed = {}
b1_deadline = min(deadline, time.monotonic() + 600)
while not probed and time.monotonic() < b1_deadline:
    if not os.path.isdir(cg):
        emit(f"K_B1_FAIL outer unit {outer} ended before any stage process was probed"); break
    for stage in STAGES:
        if stage in probed:
            continue
        unit = f"buster-bench-{job}-{attempt}-{stage}.service"
        pid, st = None, None
        for member in procs(f"{SLICE}/{unit}"):
            try:
                candidate = status(member)
            except OSError:
                continue
            if candidate["Uid"].split()[0] != "0":
                pid, st = member, candidate
                break
        if pid is None:
            continue
        try:
            uid, gid = st["Uid"].split()[0], st["Gid"].split()[0]
            mountinfo = [l for l in open(f"/proc/{pid}/mountinfo").read().splitlines() if ".lease-return" in l]
        except (OSError, KeyError):
            continue
        results = []
        for label, u, g in (("stage-uid", uid, gid), ("service-uid", "65000", "65000")):
            p = subprocess.run(["nsenter", "-t", str(pid), "-m", "-n", "--", "setpriv", f"--reuid={u}", f"--regid={g}",
                                "--clear-groups", "python3", "-c", probe, RET, sock], capture_output=True, text=True, timeout=20)
            results.append(f"{label}[{p.stdout.strip() or p.stderr.strip()[:200]}]")
        props = show(unit, "InaccessiblePaths", "User", "MainPID")
        alive = os.path.exists(f"/proc/{pid}")
        probed[stage] = {"pid": pid, "uid": uid, "results": results, "mountinfo": mountinfo, "properties": props, "alive_after": alive}
        emit(f"K_B1_STAGE_ISOLATION unit={unit} pid={pid} uid={uid} alive_after={alive} "
             f"lease_return_mounts={len(mountinfo)} inaccessible_prop_has_lease_return="
             f"{RET in props.get('InaccessiblePaths', '')} {' '.join(results)}")
    time.sleep(0.02)
json.dump(probed, open("/root/k-out/b1-stages.json", "w"), indent=2, sort_keys=True)
# Control: the same probe in the root mount namespace as the service uid reaches the socket.
p = subprocess.run(["setpriv", "--reuid=65000", "--regid=65000", "--clear-groups", "python3", "-c", probe, RET, sock],
                   capture_output=True, text=True, timeout=20)
emit(f"K_B1_CONTROL_ROOT_NAMESPACE service-uid[{p.stdout.strip() or p.stderr.strip()[:200]}]")
if not probed:
    emit("K_B1_FAIL no live stage process was probed")
KWATCH
  sudo docker exec -i "$guest" tee /root/k-peer.py >/dev/null <<'KPEER'
import array, os, socket, struct, sys, time
job, attempt = int(sys.argv[1]), int(sys.argv[2])
path = f"/var/lib/buster-bench/workspaces/results/.lease-return/{job}-{attempt}"
# BqWorkerLeaseMessage: magic[32] lease_path[193] preparation[65] pad u64 job,
# attempt, device, inode, deadline, u32 phase (RECLAIM_REQUEST = 4), pad: 344 bytes.
message = struct.pack("<32s193s65s6x5QI4x", b"BQ-LEASE-HANDOFF-V2", b"/var/lib/buster-bench/lease/host.lock",
                      b"", job, attempt, 0, 0, 0, 4)
assert len(message) == 344
cgroup = open("/proc/self/cgroup").read().strip()
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.settimeout(8)
result = {"uid": os.getuid(), "gid": os.getgid(), "cgroup": cgroup}
try:
    s.connect(path); result["connect"] = "ok"
    result["sent"] = s.send(message)
    fds = array.array("i")
    data, ancillary, flags, _ = s.recvmsg(4096, socket.CMSG_SPACE(4 * fds.itemsize))
    received = []
    for level, kind, payload in ancillary:
        if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
            received.extend(struct.unpack("%di" % (len(payload) // 4), payload[:len(payload) - len(payload) % 4]))
    for fd in received:
        os.close(fd)  # never acknowledge; closing a duplicate does not unlock
    result["recv_bytes"] = len(data); result["fds"] = len(received)
except OSError as e:
    result["error"] = repr(e)
print("K_B2_SLICE_PEER " + " ".join("%s=%s" % kv for kv in result.items()), flush=True)
KPEER
  sudo docker exec "$guest" install -m 0644 /root/k-peer.py /tmp/k-peer.py
  sudo docker exec -i "$guest" tee /root/k-locks.py >/dev/null <<'KLOCKS'
import os, sys, time
st = os.stat("/var/lib/buster-bench/lease/host.lock")
dev = "%02x:%02x:%d" % (os.major(st.st_dev), os.minor(st.st_dev), st.st_ino)
out = open("/root/k-out/locks.log", "w", buffering=1)
stop = "/root/k-out/locks.stop"
fdinfo = "/proc/%s/fdinfo/%s" % (sys.argv[2], sys.argv[3]) if len(sys.argv) > 3 else None
previous, samples, gaps, keeper_gone = None, 0, 0, False
end = time.monotonic() + float(sys.argv[1])
while time.monotonic() < end and not os.path.exists(stop):
    proc_locks = tuple(sorted(l.split(":", 1)[1].strip() for l in open("/proc/locks").read().splitlines() if dev in l))
    held = None
    if fdinfo and not keeper_gone:
        try:
            held = tuple(l.split(":", 1)[1].strip() for l in open(fdinfo).read().splitlines() if l.startswith("lock:"))
        except OSError:
            keeper_gone = True
    samples += 1
    state = ("keeper-fdinfo=" + (" | ".join(held) if held else ("KEEPER-GONE" if keeper_gone else "NO-LOCK")),
             "proc-locks=" + (" | ".join(proc_locks) or "none-visible"))
    if state != previous:
        if held is not None and not held and not keeper_gone:
            gaps += 1
        out.write("%d %d %s\n" % (time.time_ns(), time.monotonic_ns(), " ; ".join(state)))
        previous = state
    time.sleep(0.001)
out.write("SUMMARY samples=%d keeper_fdinfo_unlocked_transitions=%d keeper_gone=%s\n" % (samples, gaps, keeper_gone))
KLOCKS
  sudo docker exec -i "$guest" tee /root/k-stopper.py >/dev/null <<'KSTOP'
import os, signal, subprocess, sys, time
# Catch the restarted service's first process, stop it, attach strace -f,
# then continue it, so the recovery's own system calls are recorded.
cg = "/sys/fs/cgroup/system.slice/buster-bench.service/cgroup.procs"
old = set(sys.argv[1:])
log = open("/root/k-out/stopper.log", "w", buffering=1)
end = time.monotonic() + 60
while time.monotonic() < end:
    if not os.path.exists("/root/k-out/start-marker"):
        time.sleep(0.001)
        continue
    try:
        pids = [p for p in open(cg).read().split() if p not in old]
    except OSError:
        pids = []
    if pids:
        pid = int(pids[0])
        try:
            os.kill(pid, signal.SIGSTOP)
        except ProcessLookupError:
            log.write("%d gone-before-stop pid=%d\n" % (time.time_ns(), pid)); old.add(str(pid)); continue
        log.write("%d stopped pid=%d exe=%s\n" % (time.time_ns(), pid, os.readlink(f"/proc/{pid}/exe")))
        tracer = subprocess.Popen(["strace", "-f", "-tt", "-T", "-yy", "-s", "256", "-o", "/root/k-strace/coordinator",
                                   "-p", str(pid)], stdout=log, stderr=log)
        time.sleep(0.5)
        os.kill(pid, signal.SIGCONT)
        log.write("%d continued pid=%d\n" % (time.time_ns(), pid))
        tracer.wait()
        log.write("%d strace-exit=%s\n" % (time.time_ns(), tracer.returncode))
        break
    time.sleep(0.0002)
else:
    log.write("%d no new service process\n" % time.time_ns())
KSTOP
  kstate() {
    local tag=$1
    sudo docker exec "$guest" systemctl list-units --all --no-pager 'buster-bench*' >"$evidence/k-units-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'for u in $(systemctl list-units --all --plain --no-legend "buster-bench*" | awk "{print \$1}"); do systemctl show "$u" -p Id -p ActiveState -p SubState -p Result -p MainPID -p InvocationID -p ControlGroup -p RuntimeMaxUSec -p InaccessiblePaths; echo; done' >"$evidence/k-unit-show-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" sh -c 'ls -la /var/lib/buster-bench/workspaces/results/.lease-return /var/lib/buster-bench/workspaces/results; cat /proc/locks' >"$evidence/k-state-$tag.txt" 2>&1 || true
    sudo docker exec "$guest" journalctl --no-pager -b -o short-precise >"$evidence/k-journal-$tag.txt" 2>&1 || true
  }
  BQ=/usr/local/libexec/buster-bench-service
  BROKER=/usr/local/libexec/buster-bench-systemd-broker
  sudo docker exec "$guest" python3 /root/k-watch.py "$job" 900 2>&1 | tee "$evidence/k-watch.txt" || true
  attempt="$(sed -nE 's/^K_VAR attempt=([0-9]+)$/\1/p' "$evidence/k-watch.txt" | head -1)"
  keeper="$(sed -nE 's/^K_VAR keeper=([0-9]+)$/\1/p' "$evidence/k-watch.txt" | head -1)"
  keeper_fd="$(sed -nE 's/^K_VAR keeper_fd=([0-9]+)$/\1/p' "$evidence/k-watch.txt" | head -1)"
  echo "K_REPRO job=$job attempt=${attempt:-none} keeper=${keeper:-none}"
  if [[ -n "$keeper" ]]; then
    sudo docker exec -d "$guest" strace -f -tt -T -yy -s 256 -o /root/k-strace/keeper -p "$keeper"
    sleep 1
  fi
  if [[ -n "$attempt" ]]; then
    # (b2) a buster-bench peer inside buster-bench.slice, reaching the socket
    # directly (no InaccessiblePaths), sends a well-formed reclaim request.
    sudo docker exec "$guest" systemd-run --quiet --wait --collect --pipe --unit="k-slice-peer-$job" \
      --slice=buster-bench.slice --uid=buster-bench --gid=buster-bench \
      python3 /tmp/k-peer.py "$job" "$attempt" 2>&1 | tee "$evidence/k-slice-peer.txt" || true
    sudo docker exec "$guest" sh -c "test -S /var/lib/buster-bench/workspaces/results/.lease-return/$job-$attempt && echo K_B2_KEEPER_STILL_SERVING=yes || echo K_B2_KEEPER_STILL_SERVING=no" | tee -a "$evidence/k-slice-peer.txt" || true
    kstate before-restart
    # (c) the service loses its reference without cleanup (SIGKILL to MainPID),
    # then restarts while the outer unit and its keeper are alive.
    oldpids="$(sudo docker exec "$guest" cat /sys/fs/cgroup/system.slice/buster-bench.service/cgroup.procs | tr '\n' ' ')"
    sudo docker exec "$guest" rm -f /root/k-out/locks.stop
    sudo docker exec -d "$guest" python3 /root/k-locks.py 240 "$keeper" "$keeper_fd"
    sudo docker exec -d "$guest" python3 /root/k-stopper.py $oldpids
    sleep 1
    echo "K_C_KILL_SERVICE $(date +%s%N) old_pids=$oldpids"
    sudo docker exec "$guest" systemctl kill --kill-whom=main --signal=SIGKILL buster-bench.service || true
    for _ in $(seq 1 100); do
      state="$(sudo docker exec "$guest" systemctl is-active buster-bench.service || true)"
      if [[ "$state" != active && "$state" != deactivating ]]; then break; fi
      sleep 0.1
    done
    echo "K_C_SERVICE_DOWN $(date +%s%N) state=$state"
    sudo docker exec "$guest" sh -c "systemctl is-active buster-bench-$job-$attempt.service; ls -la /var/lib/buster-bench/workspaces/results/.lease-return; test -d /proc/$keeper && echo keeper-alive || echo keeper-gone" | tee "$evidence/k-after-kill.txt" || true
    sudo docker exec "$guest" touch /root/k-out/start-marker
    sudo docker exec "$guest" systemctl start --no-block buster-bench.service
    echo "K_C_SERVICE_START $(date +%s%N)"
    for _ in $(seq 1 90); do
      sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway result "$job" >"$evidence/k-gateway-after-restart.txt" 2>&1 || true
      if grep -q 'phase=finished' "$evidence/k-gateway-after-restart.txt"; then break; fi
      sleep 1
    done
    cat "$evidence/k-gateway-after-restart.txt"
    sudo docker exec "$guest" sh -c "systemctl show -p ActiveState -p Result buster-bench-$job-$attempt.service; test -d /proc/$keeper && echo K_A_KEEPER_STOPPED=no || echo K_A_KEEPER_STOPPED=yes; test -e /var/lib/buster-bench/workspaces/results/.lease-return/$job-$attempt && echo K_A_SOCKET_REMOVED=no || echo K_A_SOCKET_REMOVED=yes" | tee "$evidence/k-after-recovery.txt" || true
    newpid="$(sudo docker exec "$guest" systemctl show -p MainPID --value buster-bench.service || true)"
    sudo docker exec "$guest" sh -c "ls -l /proc/$newpid/fd 2>&1 | grep host.lock; cat /proc/locks" | tee -a "$evidence/k-after-recovery.txt" || true
    sudo docker exec "$guest" touch /root/k-out/locks.stop
    sudo docker exec "$guest" pkill -INT -x strace || true
    sleep 2
    sudo docker exec "$guest" sh -c 'sed "s/^/K_C_LOCKS /" /root/k-out/locks.log' || true
    kstate after-recovery
    # (d) a second job: explicit v2 TERM on a live stage, then KILL on the outer unit.
    key2="issue1162-${GITHUB_RUN_ID}-k2"
    sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway submit "$key2" "$baseline" "$subject" | tee "$evidence/k-submit2.txt" || true
    job2="$(sed -nE 's/^job=([0-9]+) .*/\1/p' "$evidence/k-submit2.txt" | head -1)"
    if [[ -n "$job2" ]]; then
      sudo docker exec -i "$guest" python3 - "$job2" <<'KWAIT2' | tee "$evidence/k-job2-wait.txt" || true
import os, re, sys, time
job = sys.argv[1]; SLICE = "/sys/fs/cgroup/buster.slice/buster-bench.slice"
end = time.monotonic() + 600
while time.monotonic() < end:
    names = os.listdir(SLICE) if os.path.isdir(SLICE) else []
    if any(re.fullmatch(r"buster-bench-%s-[0-9]+\.service" % job, n) for n in names):
        seen = True
    elif "seen" in globals():
        print("K_D_FAIL second job's outer unit ended before a stage was live", flush=True); break
    stage = [n for n in names if re.fullmatch(r"buster-bench-%s-[0-9]+-(base|candidate)-(generate|build)\.service" % job, n)
             and open(f"{SLICE}/{n}/cgroup.procs").read().split()]
    if stage:
        outer = re.sub(r"-(base|candidate)-(generate|build)\.service$", ".service", stage[0])
        print(f"K_VAR stage={stage[0]}\nK_VAR outer={outer}", flush=True); break
    time.sleep(0.02)
KWAIT2
      stage2="$(sed -nE 's/^K_VAR stage=(.+)$/\1/p' "$evidence/k-job2-wait.txt" | head -1)"
      outer2="$(sed -nE 's/^K_VAR outer=(.+)$/\1/p' "$evidence/k-job2-wait.txt" | head -1)"
      if [[ -n "$stage2" ]]; then
        set +e
        sudo docker exec "$guest" runuser -u buster-bench -g buster-bench -- $BROKER signal "$stage2" TERM >"$evidence/k-d-term.txt" 2>&1
        term_status=$?
        sudo docker exec "$guest" runuser -u buster-bench -g buster-bench -- $BROKER signal "$outer2" KILL >"$evidence/k-d-kill.txt" 2>&1
        kill_status=$?
        set -e
        echo "K_D_BROKER_TERM unit=$stage2 exit=$term_status" | tee -a "$evidence/k-d-term.txt"
        echo "K_D_BROKER_KILL unit=$outer2 exit=$kill_status" | tee -a "$evidence/k-d-kill.txt"
        cat "$evidence/k-d-term.txt" "$evidence/k-d-kill.txt"
        sleep 2
        sudo docker exec "$guest" systemctl show -p ActiveState -p Result "$stage2" "$outer2" | tee -a "$evidence/k-d-kill.txt" || true
        for _ in $(seq 1 120); do
          sudo docker exec "$guest" runuser -u buster-bench -- $BQ gateway result "$job2" >"$evidence/k-gateway-job2.txt" 2>&1 || true
          if grep -q 'phase=finished' "$evidence/k-gateway-job2.txt"; then break; fi
          sleep 1
        done
        cat "$evidence/k-gateway-job2.txt"
      fi
    fi
    kstate final
  fi
  sudo docker exec "$guest" tar -C /root -czf - k-out k-strace >"$evidence/k-artifacts.tgz" 2>"$evidence/k-artifacts-tar.log" || true
  sudo docker exec "$guest" journalctl --no-pager -b -o json -u 'buster-bench-systemd-broker@*.service' >"$evidence/k-broker-journal.jsonl" 2>/dev/null || true
  python3 - "$evidence/k-broker-journal.jsonl" <<'KFRAMES' | tee "$evidence/k-broker-frames.txt" || true
import json, re, sys
rows = []
for line in open(sys.argv[1]):
    try:
        m = json.loads(line).get("MESSAGE", "")
    except ValueError:
        continue
    if isinstance(m, str) and "BQ-BROKER-DIAG-V1 REQUEST" in m:
        f = dict(re.findall(r"(\w+)=(-?\d+)", m))
        rows.append((f.get("recv"), f.get("operation"), f.get("stage"), f.get("job"), f.get("attempt"), f.get("command_valid")))
for r in rows:
    print("K_D_FRAME recv=%s operation=%s stage=%s job=%s attempt=%s command_valid=%s" % r)
print("K_D_FRAMES total=%d recv184=%d other=%d" % (len(rows), sum(r[0] == "184" for r in rows), sum(r[0] != "184" for r in rows)))
KFRAMES
  echo "BQ_K_REPRO_DONE job=$job"
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
